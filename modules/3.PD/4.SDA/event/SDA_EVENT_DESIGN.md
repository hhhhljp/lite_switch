# SDA 事件处理框架设计

> 版本：v1.2 | 日期：2026-06-11

## 1. 设计目标

SDA 作为 lite_switch 中唯一与 SDK 耦合的模块，需要在进程内部实现两套正交的事件处理机制：

| 机制 | 路径 | 时延 | 适用场景 |
|------|------|------|----------|
| **事件路由** | SDK → event 模块 → 子模块 → Redis → 上层 | 毫秒级 | 状态同步、Web 推送、协议控制 |
| **动作链** | SDK → event 模块 → 子模块 → 直调目标函数 | 微秒级 | ECMP 快切、MAC 表联动刷新 |

核心原则：

- **模块自治**：每个子模块在自身代码中声明处理的事件类型，event 模块统一收集路由
- **零拷贝分发**：事件路由基于函数表 O(n) 匹配，动作链按注册顺序同步调用
- **低耦合**：子模块不感知其他模块的存在，通过动作链实现模块间松耦合联动
- **渐进扩展**：新增事件类型只需在对应子模块中添加 handler 并在路由表注册

---

## 2. 架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                        SDA 进程                              │
│                                                              │
│  SDK 事件线程 (fmGlobalEventHandler)                          │
│       │                                                      │
│       ▼                                                      │
│  ┌─────────────────────────────────────────────────────┐     │
│  │              event 模块 (轻量路由器)                  │     │
│  │                                                     │     │
│  │  sda_event_handler(event, sw, ptr)                  │     │
│  │       │                                             │     │
│  │       ▼                                             │     │
│  │  sda_event_dispatch(router, event, sw, ptr)         │     │
│  │       │                                             │     │
│  │       ├─ 匹配路由表 → 调用子模块 handler              │     │
│  │       │   (同一事件可触发多个 handler)               │     │
│  │       │                                             │     │
│  │       └─ 未匹配 → 日志记录 + 丢弃                    │     │
│  └─────────────────────────────────────────────────────┘     │
│       │                                                      │
│       │  子模块 handler 内部：                                │
│       │                                                      │
│       ├── ① 填充 proto → mw_set_message() → Redis            │
│       │       (状态同步，毫秒级)                              │
│       │                                                      │
│       └── ② SDA_ACTION_FIRE(chain, data)                     │
│               (直调其他模块，微秒级)                           │
│                                                              │
│  ┌─────────────────────┐    ┌─────────────────────┐          │
│  │  intf 模块           │    │  route 模块（将来）   │          │
│  │  - 提供动作链        │    │  - 订阅动作链        │          │
│  │  - 事件 handler      │    │  - 事件 handler      │          │
│  │  - Redis 生产者      │    │  - Redis 生产者      │          │
│  └─────────────────────┘    └─────────────────────┘          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
                            │
                    Redis Pub/Sub
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
         switch-web      2.PI         Bridge
        (SSE 推送)    (协议控制)    (硬件抽象)
```

---

## 3. 目录结构

```
modules/3.PD/4.SDA/
├── event/                          ★ 新增：事件框架模块
│   ├── SDA_EVENT_DESIGN.md         # 本设计文档
│   ├── CMakeLists.txt              # 编译为 libsda_event.a
│   ├── sda_event.h                 # 公共头文件（宏 + API）
│   ├── sda_event.c                 # 路由器实现
│   ├── sda_event_entry.c           # SDK 回调入口 + 初始化
│   ├── sda_action.h                # 动作链公共头文件（集中声明）
│   ├── sda_action.c                # 动作链实现
│   ├── sda_event_types.h           # 标准事件数据类型定义
│   ├── sda_event_sim.h / .c        ★ NO_HW 事件模拟框架
│   └── CMakeLists.txt
│
├── intf/                           # 现有：接口模块
│   ├── intf_api.h / .c             # 接口状态查询（SDK 调用）
│   ├── intf_cb.h / .c              # Redis 回调注册（现有，保留）
│   ├── intf_event.h / .c           ★ 新增：事件 handler + 注册函数
│   └── intf_action.c               ★ 新增：动作链定义与初始化
│
├── route/                          # 将来：路由模块
│   ├── route_event.c               # 事件 handler
│   └── route_action.c              # 动作链订阅（ECMP 快切等）
│
├── include/
│   └── sda_core.h                  # 现有：核心定义（需扩展）
│
├── init/
│   ├── sdk_init.h / .c             # 现有：SDK 初始化（需修改）
│
├── main.c                          # 现有：主程序（需修改启动流程）
└── CMakeLists.txt                  # 现有：顶层 CMake
```

---

## 4. 机制一：事件路由框架

### 4.1 模块自注册

每个子模块在自己的头文件中用宏声明事件 handler：

```c
// intf/intf_event.h
#ifndef INTF_EVENT_H
#define INTF_EVENT_H

#include "event/sda_event.h"

// ★ 模块声明自己能处理的事件
SDA_EVENT_HANDLER(FM_EVENT_PORT,            intf_on_port_event);
SDA_EVENT_HANDLER(FM_EVENT_CABLE_MISMATCH,  intf_on_cable_event);

#endif
```

宏展开后等价于：

```c
// 声明 handler 函数
static void intf_on_port_event(mw_context_t *ctx, fm_int sw, void *payload);
static void intf_on_cable_event(mw_context_t *ctx, fm_int sw, void *payload);

// 标记：这些 handler 需要在 event 模块 init 时注册
// （具体机制见 4.3）
```

### 4.2 路由器实现

```c
// event/sda_event.h

/* ── 事件处理函数签名 ── */
typedef void (*sda_event_fn)(mw_context_t *ctx, fm_int sw, void *payload);

/* ── 路由条目 ── */
typedef struct {
    fm_int       event_type;   // FM_EVENT_PORT 等 SDK 事件常量
    sda_event_fn handler;      // 子模块处理函数
} sda_event_route_t;

/* ── 路由器 ── */
typedef struct {
    sda_event_route_t *routes;
    int                count;
    int                cap;
} sda_event_router_t;

/* ── API ── */
void sda_event_router_init(sda_event_router_t *router);
void sda_event_router_add(sda_event_router_t *router,
                          fm_int event, sda_event_fn fn);
void sda_event_dispatch(sda_event_router_t *router,
                        mw_context_t *ctx,
                        fm_int event, fm_int sw, void *payload);
```

```c
// event/sda_event.c

void sda_event_dispatch(sda_event_router_t *router,
                        mw_context_t *ctx,
                        fm_int event, fm_int sw, void *payload)
{
    int matched = 0;
    for (int i = 0; i < router->count; i++) {
        if (router->routes[i].event_type == event) {
            router->routes[i].handler(ctx, sw, payload);
            matched++;
            // 不 break — 同一事件可注册多个 handler
        }
    }
    if (matched == 0) {
        dzlog_debug("event: no handler for event=0x%x sw=%d", event, sw);
    }
}
```

### 4.3 路由收集方式：显式注册（推荐）

不依赖 linker section（`__attribute__((section))`），而是在 event 模块初始化时由各子模块显式注册。可移植性好，可读性高。

```c
// event/sda_event_entry.c — SDK 回调入口 + 初始化

static sda_event_router_t g_router;

/* ── 子模块注册函数的前向声明 ── */
extern void intf_event_register(sda_event_router_t *router);
// extern void route_event_register(sda_event_router_t *router);  // 将来

int sda_event_init(void)
{
    sda_event_router_init(&g_router);

    // ★ 各模块在此显式注册自己的 handler
    intf_event_register(&g_router);
    // route_event_register(&g_router);   // 将来

    dzlog_info("event: router initialized, %d route(s)", g_router.count);
    return 0;
}

/* ── SDK 全局回调入口（在 sdk_init_all 之后注册） ── */
void sda_event_handler(fm_int event, fm_int sw, void *ptr)
{
    sda_event_dispatch(&g_router, g_sda_cb_ctx, event, sw, ptr);
}
```

各子模块的注册函数：

```c
// intf/intf_event.c

void intf_event_register(sda_event_router_t *router)
{
    SDA_EVENT_REGISTER(router, FM_EVENT_PORT,           intf_on_port_event);
    SDA_EVENT_REGISTER(router, FM_EVENT_CABLE_MISMATCH, intf_on_cable_event);
}

static void intf_on_port_event(mw_context_t *ctx, fm_int sw, void *payload)
{
    fm_eventPort *pe = (fm_eventPort *)payload;
    // ... 填充 proto + mw_set_message + 触发动作链 ...
}
```

### 4.4 SDA_EVENT_HANDLER 宏的真正职责

`SDA_EVENT_HANDLER` 宏放在头文件中，起两个作用：

1. **声明 handler 函数**（避免跨文件 extern 声明散落）
2. **文档化**：一眼看出模块支持哪些事件

```c
// event/sda_event.h

#define SDA_EVENT_HANDLER(_event, _func) \
    void _func(mw_context_t *ctx, fm_int sw, void *payload)

#define SDA_EVENT_REGISTER(_router, _event, _func) \
    sda_event_router_add((_router), (_event), (_func))
```

### 4.5 初始化顺序：消除竞态窗口

> ⚠️ **实物验证待办**：以下初始化流程尚未在真实硬件上验证。后续需专门验证 Phase 2 中 `fmInitialize(sda_event_handler)` 直接注册的可行性。

**核心思路**：交换 Phase 1 和 Phase 2 的职责，在 `fmInitialize` 之前完成事件框架和 Redis 连接初始化，使 `fmInitialize` 直接注册最终 handler，从根本上消除接管间隙。

```
新顺序：
  Phase 1: （空 — 硬件初始化解耦到 Phase 2）
  Phase 2: Redis连接 + 事件路由器 + 动作链 + SDK初始化
           └─ fmInitialize(sda_event_handler)  ← 直接注册，无接管
  Phase 3: 接口轮询上报

结果：竞态窗口不存在 ✓
```

**`sda_event_handler` 特殊处理系统事件**：

`fmInitialize` 之前事件框架已就绪。`sda_event_handler` 中直接处理 `FM_EVENT_SWITCH_INSERTED/REMOVED` 等生命周期事件（不经过路由器），其余的走 `sda_event_dispatch` 分发到子模块：

```c
void sda_event_handler(fm_int event, fm_int sw, void *ptr)
{
    /* ── 系统生命周期事件 — 在此直接处理 ── */
    switch (event) {
    case FM_EVENT_SWITCH_INSERTED:
        fmSupportInitialize(sw);
        fmReleaseSemaphore(sda_get_insert_sem());
        return;
    case FM_EVENT_SWITCH_REMOVED:
        dzlog_warn("sda: switch %d removed", sw);
        return;
    }

    /* ── 模块事件 — 走路由器分发 ── */
    sda_event_dispatch(&g_router, g_sda_cb_ctx, event, sw, ptr);
}
```

**`fmInitialize` 不会立即调用 handler**：`fmInitialize` 只是启动事件线程并将函数指针存下来，事件循环在 `fmCaptureSemaphore(&startGlobalEventHandler, ...)` 处等待初始化完成。

---

## 5. 机制二：组件间动作链（低时延路径）

### 5.1 设计动机

Redis Pub/Sub 的端到端时延包含：Redis 写 → keyspace 通知 → Pub/Sub 推送 → 接收端 mw_poll 轮询。对于 ECMP 快切等场景，毫秒级延迟意味着已发生丢包。需要一条**微秒级**的旁路。

### 5.2 动作链统一管理

为避免各模块分散声明 `extern` 导致编译依赖混乱，所有动作链在 `event/sda_action.h` 中集中声明。每个链附带注释说明：

- **提供者**（哪个模块初始化）
- **触发时机**（什么事件触发）
- **数据格式**（`event_data` 类型）
- **典型消费者**（谁会订阅）
- **约束**（非阻塞、短路径等）

```c
// event/sda_action.h — 所有模块对外暴露的动作链声明
#ifndef SDA_ACTION_H
#define SDA_ACTION_H

#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════
 * 动作链注册表
 *
 * 管理规则：
 *   1. 每个链由唯一的 provider 模块声明和初始化（sda_action_chain_init），
 *      链的定义（全局变量）放在 provider 模块的 *_action.c 中。
 *   2. consumer 模块通过 sda_action_chain_add() 订阅，订阅发生在 Phase 2
 *      初始化阶段（单线程，无需加锁）。
 *   3. provider 在事件 handler 中通过 SDA_ACTION_FIRE() 触发。
 *   4. 动作链回调在 SDK 事件线程（fmGlobalEventHandler）上下文中同步执行，
 *      因此必须非阻塞、短路径（数十微秒内返回）。
 *   5. 动作链回调不得调用 mw_set_message()——写 Redis 是事件 handler
 *      主逻辑的职责，不在动作链中执行。
 * ═══════════════════════════════════════════════════════════════ */

/* ── 动作链不透明句柄 ── */
typedef struct sda_action_chain_t sda_action_chain_t;

/* ── 标准事件数据类型 ── */

/** 端口事件数据（link up / down） */
typedef struct {
    uint32_t sw;        // 交换机号
    uint32_t port;      // 逻辑端口号
    bool     link_up;   // true = up, false = down
} sda_port_event_data_t;

/* ═══════════════════════════════════════════════════════════════
 * 已注册的动作链
 * ═══════════════════════════════════════════════════════════════ */

/* ──────────── intf 模块提供 ──────────── */

/** port_up 链
 *  provider:  intf 模块
 *  触发时机: SDK FM_EVENT_PORT + linkStatus == FM_PORT_STATUS_LINK_UP
 *  data:      sda_port_event_data_t (link_up = true)
 *  消费者示例: route 模块 (ECMP member restore) */
extern sda_action_chain_t g_port_up_chain;

/** port_down 链
 *  provider:  intf 模块
 *  触发时机: SDK FM_EVENT_PORT + linkStatus == FM_PORT_STATUS_LINK_DOWN
 *  data:      sda_port_event_data_t (link_up = false)
 *  消费者示例: route 模块 (ECMP fast reroute 摘除故障端口)
 *             bridge 模块 (MA table flush for the port) */
extern sda_action_chain_t g_port_down_chain;

/** port_link_change 链
 *  provider:  intf 模块
 *  触发时机: 任何端口 link 状态变化（up / down / autoneg 等）
 *  data:      sda_port_event_data_t
 *  注意:     此链在 port_up / port_down 之前触发，
 *            用于需要感知所有变化但不区分方向的消费者 */
extern sda_action_chain_t g_port_link_change_chain;

#endif /* SDA_ACTION_H */
```

### 5.3 动作链数据结构

```c
// event/sda_action.c 内部

/* ── 动作回调签名 ── */
typedef void (*sda_action_fn)(void *action_ctx, const void *event_data);

/* ── 动作钩子 ── */
typedef struct {
    const char    *name;         // 调试用标识
    sda_action_fn  fn;           // 回调函数
    void          *ctx;          // 回调上下文
} sda_action_hook_t;

/* ── 动作链 ── */
struct sda_action_chain_t {
    sda_action_hook_t *hooks;
    int                count;
    int                cap;
    const char        *chain_name;   // 调试用
};

/* ── API ── */
void sda_action_chain_init(sda_action_chain_t *chain, const char *name);
void sda_action_chain_add(sda_action_chain_t *chain,
                          const char *hook_name,
                          sda_action_fn fn, void *ctx);
void sda_action_chain_fire(sda_action_chain_t *chain,
                           const void *event_data);

/* ── 便捷宏 ── */
#define SDA_ACTION_FIRE(_chain, _data) \
    sda_action_chain_fire((_chain), (_data))
```

### 5.4 动作链使用规范

**动作链的提供者**声明链（通常是事件的生产者模块）：

```c
// intf/intf_action.c — intf 模块提供三个动作链

sda_action_chain_t g_port_up_chain;
sda_action_chain_t g_port_down_chain;
sda_action_chain_t g_port_link_change_chain;

void intf_action_init(void)
{
    sda_action_chain_init(&g_port_up_chain,     "port_up");
    sda_action_chain_init(&g_port_down_chain,   "port_down");
    sda_action_chain_init(&g_port_link_change_chain, "port_link_change");
}
```

**动作链的消费者**订阅（其他模块注册回调）：

```c
// route/route_action.c — route 模块订阅 port_down 链

static void ecmp_fast_reroute(void *ctx, const void *event_data)
{
    (void)ctx;
    const sda_port_event_data_t *d = (const sda_port_event_data_t *)event_data;
    ecmp_remove_member(d->sw, d->port);   // 微秒级摘除故障端口
}

void route_action_init(void)
{
    sda_action_chain_add(&g_port_down_chain, "ecmp_reroute",
                         ecmp_fast_reroute, NULL);
}
```

**动作链的触发**（在事件 handler 中）：

```c
// intf/intf_event.c — intf 的事件 handler 中触发

static void intf_on_port_event(mw_context_t *ctx, fm_int sw, void *payload)
{
    fm_eventPort *pe = (fm_eventPort *)payload;

    // 1. 构造事件数据
    sda_port_event_data_t data = {
        .sw = (uint32_t)sw,
        .port = (uint32_t)pe->port,
        .link_up = (pe->linkStatus == FM_PORT_STATUS_LINK_UP),
    };

    // 2. ★ 先触发低时延动作链（微秒级，同步执行）
    SDA_ACTION_FIRE(&g_port_link_change_chain, &data);
    if (data.link_up)
        SDA_ACTION_FIRE(&g_port_up_chain, &data);
    else
        SDA_ACTION_FIRE(&g_port_down_chain, &data);

    // 3. ★ 再写 Redis（毫秒级，异步通知上层）
    // ... 填充 PdInterfaceEntry + mw_set_message ...
}
```

---

## 6. 与现有代码的集成

> ⚠️ **实物验证待办**：新的 Phase 1/2 交换流程尚未在真实硬件上验证。

### 6.1 初始化顺序调整

**核心变更**：交换 Phase 1（硬件初始化）和 Phase 2（软件初始化）的职责，使事件框架和 Redis 在 `fmInitialize` 之前就绪，`fmInitialize` 直接注册 `sda_event_handler`，无需 bootstrap stub 和后续 `fmSetEventHandler` 接管。

```
调整后：
  Phase 1: （空 — 硬件初始化解耦到 Phase 2）
  Phase 2: 软件初始化
    ├─ sda_sw_redis_connect()          ← 主线程 Redis
    ├─ sda_event_init_and_takeover()   ← 回调线程 Redis + 事件路由器
    ├─ intf_action_init()              ← 动作链初始化
    ├─ route_action_init()             ← 订阅动作链 (将来)
    ├─ interface_cb_register()         ← Redis Pub/Sub 回调
    └─ sdk_init_all()                  ← ★ 最后：fmInitialize(sda_event_handler)
  Phase 3: sda_interface_poll_all()    ← 接口轮询上报
```

### 6.2 sdk_init.c 的改造

```c
// sdk_init.c — 改造后

// ★ 外部声明：sda_event_handler 由 event 模块提供
extern void sda_event_handler(fm_int event, fm_int sw, void *ptr);

// ★ 信号量暴露给 event 模块
extern fm_semaphore *sda_get_insert_sem(void);

int sdk_init_all(void)
{
    fm_status st;
    fm_int    sw, next_sw, found;
    fm_timestamp timeout;
    fm_switchInfo info;

    // ... fmOSInitialize() ...（不变）

    // ★ 直接注册 sda_event_handler — 事件框架在 Phase 2 前面已就绪
    st = fmInitialize(sda_event_handler);
    if (st != FM_OK) return SDA_ERR;

    st = fmSetProcessEventMask(0xFFFFFFFF);
    if (st != FM_OK) return SDA_ERR;

    // ... 等待 switch inserted + 上电 + fmPlatformPortInitialize ...
    // （后续流程与现在一致，只是不再有 bootstrap handler）
    return SDA_OK;
}
```

### 6.3 main.c 三阶段初始化

```c
// main.c — 扩展后

/* ── Phase 1: 硬件初始化（空） ── */
static const hw_init_fn_t hw_init_fns[] = {
    NULL  // ★ 硬件初始化解耦到 Phase 2 末尾
};

/* ── Phase 2: 软件初始化 ── */
static const sw_init_fn_t sw_init_fns[] = {
#ifndef SDA_NO_HW
    sda_sw_redis_connect,          // ① 主线程 Redis 连接
    sda_event_init_and_takeover,   // ② 事件框架 + 回调线程 Redis
    intf_action_init,              // ③ 动作链初始化
    // route_action_init,          // ④ (将来) route 订阅动作链
    interface_cb_register,         // ⑤ Redis Pub/Sub 回调
    sdk_init_all,                  // ⑥ ★ SDK 初始化（fmInitialize 直接注册）
#else
    sda_sw_redis_connect,
    sda_event_init_and_takeover,
    intf_action_init,
    interface_cb_register,
    // NO_HW 不调 sdk_init_all
#endif
    NULL
};

/* ── Phase 3: 自定义初始化任务 ── */
static const custom_init_fn_t custom_init_fns[] = {
#ifndef SDA_NO_HW
    sda_interface_poll_all,        // 接口轮询上报
#endif
    NULL
};
```

**关键说明**：

- `sdk_init_all` 排在 Phase 2 **最后**——此时事件路由器、Redis（双连接）、动作链已全部就绪
- `fmInitialize(sda_event_handler)` 直接注册，无 bootstrap → 无接管 → **无竞态**
- `sda_event_handler` 中，`FM_EVENT_SWITCH_INSERTED` 在入口处直接处理（`fmSupportInitialize` + `fmReleaseSemaphore`），其余事件走路由器分发

### 6.4 全局上下文扩展


## 7. 完整数据流示例：Link Down 事件

```
时间线 (μs)  │  处理步骤
─────────────┼─────────────────────────────────────────────────────
             │
  T+0        │  硬件检测到光模块拔出 → 产生中断
             │
  T+10       │  fmInterruptHandler 读寄存器 → 状态机转换
             │  fm10000_portSmStates: UP → DOWN
             │
  T+20       │  fm10000SendLinkUpDownEventV2(sw=0, physPort=5, linkUp=FALSE)
             │  → fmAllocateEvent → 填充 fm_eventPort
             │  → fmSendThreadEvent → 入队
             │
  T+50       │  fmGlobalEventHandler 线程出队
             │  → 内部预处理 (LAG/LBG/Mirror/MA Flush)
             │
  T+80       │  sda_event_handler(FM_EVENT_PORT, sw=0, ptr)
             │  → sda_event_dispatch(router, ...)
             │    → 匹配路由表: FM_EVENT_PORT → intf_on_port_event
             │
  T+85       │  intf_on_port_event(ctx, 0, payload):
             │
             │  ★ ① 动作链（微秒级）:
             │    SDA_ACTION_FIRE(&g_port_down_chain, &data)
             │    → ecmp_fast_reroute()  ← route 模块
             │      → ecmp_remove_member(sw=0, port=5)
             │
             │    SDA_ACTION_FIRE(&g_port_link_change_chain, &data)
             │    → 其他订阅者...
             │
  T+90       │  ★ ② Redis 写入（毫秒级）:
             │    mw_set_message(cb_ctx, key, entry)
             │    → SET s_pd_interface/<binary> <proto_entry>
             │    → Redis keyspace 通知触发
             │
  T+200      │  Redis Pub/Sub:
  (approx)   │    → mw_poll(ctx) 在 switch-web 进程中触发
             │    → on_interface("set", entry, ...)
             │    → cache_upsert + sse_broadcast
             │    → 浏览器 SSE 推送，页面更新
             │
```

---

## 8. 动作链订阅/触发时序

动作链的注册必须在事件首次触发之前完成。由于 Phase 1 和 Phase 2 已交换，（含 ）在 Phase 2 末尾执行，事件框架和动作链在  之前已全部就绪：

```
Phase 1: （空）
    │
Phase 2: sda_sw_redis_connect()       ← ① 主线程 Redis
    │    sda_event_init_and_takeover() ← ② 回调线程 Redis + 事件路由器
    │
    ├──── 以下动作链相关调用按模块依赖顺序：
    │
    │    intf_action_init()            ← ③ intf 初始化动作链（空链，等待订阅）
    │    route_action_init()           ← ④ route 订阅 port_down/up 链 ★
    │    bridge_action_init()          ← ④ bridge 订阅 port_down 链 ★
    │    interface_cb_register()       ← ⑤ Redis Pub/Sub 回调
    │
    │    sdk_init_all()                ← ⑥ ★ fmInitialize(sda_event_handler)
    │                                      SDK 事件线程启动，handler 直接到位
Phase 3: sda_interface_poll_all()     ← 首次轮询（动作链 + 事件框架已就绪）
    │
事件循环: mw_poll()                    ← 之后的事件才触发动作链
```

> ⚠️ **实物验证待办**：以上时序尚未在真实硬件上验证。

**关键约束**：

- 动作链在 SDK 事件线程中执行（fmGlobalEventHandler 的线程上下文）
- 动作链回调**不得阻塞**（不得 sleep、不得等待锁、不得调用可能阻塞的 SDK API）
- 动作链回调**不得调用 mw_set_message**（这是Redis路径的职责，避免事件线程被Redis阻塞）

---

## 9. 宏定义总览

```c
// event/sda_event.h — 事件路由
#define SDA_EVENT_HANDLER(_event, _func)       // 声明 handler（放 .h）
#define SDA_EVENT_REGISTER(_router, _event, _func)  // 注册 handler（放 _register()）

// event/sda_action.h — 动作链
#define SDA_ACTION_FIRE(_chain, _data)         // 触发动作链（在 handler 中调用）

// 使用示例
void sda_action_chain_init(chain, name);       // 初始化链（提供者调用）
void sda_action_chain_add(chain, name, fn, ctx); // 注册回调（消费者调用）
```

---

## 10. 动作链设计约束清单

| 约束 | 说明 |
|------|------|
| **非阻塞** | 回调在 SDK 事件线程中同步执行，不得 sleep/等锁/调阻塞 API |
| **短路径** | 单个回调应在数十微秒内返回，复杂逻辑应异步化 |
| **不得调 mw_set_message** | 动作链和 Redis 路径是正交的，写 Redis 由事件 handler 的主逻辑负责 |
| **初始化时注册** | 动作链的订阅必须在 Phase 2 完成，Phase 3 之前 |
| **上下文安全** | `action_ctx` 由订阅者传入，回调中只读使用 |

---

## 11. NO_HW 事件模拟框架

### 11.1 设计目标

在 `SDA_NO_HW=ON` 模式下，没有 SDK、没有 `fm_eventPort`、没有 `fmGlobalEventHandler`。需要一套替代机制来：

1. 按固定周期（默认 1 秒）产生模拟事件
2. 允许各子模块注册自己的模拟事件生成器
3. 模拟器直接调用子模块的 Redis 生产路径（不伪造 SDK 事件结构体）
4. 上层模块（switch-web、PI 等）无感知——对它们来说，就是 Redis 里有数据在变化

### 11.2 模拟器 API

```c
// event/sda_event_sim.h
#ifndef SDA_EVENT_SIM_H
#define SDA_EVENT_SIM_H

#include "middleware.h"

/* ── 模拟事件生成器签名 ── */
typedef void (*sda_sim_event_fn)(mw_context_t *ctx);

/* ── API ── */

/** 注册一个模拟事件生成器
 *  生成器在 sim_loop 的每次 tick 被调用（顺序同注册顺序）。
 *  必须在 sda_sim_run 之前调用（Phase 2）。 */
void sda_sim_register(sda_sim_event_fn fn);

/** 启动模拟事件循环（阻塞）
 *  每隔 interval_ms 毫秒触发一次所有已注册的生成器。
 *  在当前设计中替代 mw_poll 事件循环，由 main.c 根据 SDA_NO_HW 选择。
 *
 *  @ctx:      Redis 连接上下文（主线程的 g_sda_ctx）
 *  @interval_ms: 触发间隔毫秒，默认 1000 */
void sda_sim_run(mw_context_t *ctx, int interval_ms);

#endif
```

### 11.3 使用示例：模拟 link 翻转

```c
// intf/intf_event.c — NO_HW 部分

#ifdef SDA_NO_HW

#include "event/sda_event_sim.h"
#include "PD/interface/interface.pb-c.h"

static void intf_sim_link_toggle(mw_context_t *ctx)
{
    static int toggle = 0;
    toggle = !toggle;

    PdInterface__PdInterfaceKey   key   = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
    PdInterface__PdInterfaceEntry entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;
    key.sw   = 0;
    key.port = 0;

    entry.index      = &key;
    entry.port_name  = "sim-port-0";
    entry.speed      = PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_25G;
    entry.admin_mode = PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP;
    entry.link_state = toggle ? PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP
                              : PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN;
    entry.mtu        = 1500;

    dzlog_info("sim: port 0 link %s", toggle ? "UP" : "DOWN");
    mw_set_message(ctx, (const ProtobufCMessage *)&key,
                   (const ProtobufCMessage *)&entry);
}

void intf_sim_register_all(void)
{
    sda_sim_register(intf_sim_link_toggle);
    // 将来可注册更多:
    // sda_sim_register(intf_sim_cable_event);
}

#endif /* SDA_NO_HW */
```

### 11.4 main.c 集成

```c
// main.c — Phase 3 之后的事件循环分支

#ifndef SDA_NO_HW
    /* ── 硬件模式：SDK 事件 + Redis Pub/Sub ── */
    while (g_running) {
        mw_poll(g_sda_ctx, 1000);
    }
#else
    /* ── 仿真模式：周期模拟事件循环 ── */
    dzlog_info("sda: entering NO_HW simulation loop (interval=1000ms)");
    sda_sim_run(g_sda_ctx, 1000);
#endif
```

### 11.5 与事件路由框架的关系

NO_HW 模拟器**不经过**事件路由框架。原因：

- 没有 SDK 就没有 `fm_event_handler` 回调
- 模拟生成器直接调用 `mw_set_message()` → 这正是事件 handler 中"写 Redis"这一步
- Redis keyspace 通知自然触发上层模块的 `mw_poll` 消费

模拟器本质上是**事件路由框架中 handler 的 Redis 写路径的替身**，不涉及 SDK 事件结构体转换。

---

## 12. 改造清单

### 12.1 新增文件

| 文件 | 职责 |
|------|------|
| `event/SDA_EVENT_DESIGN.md` | 本设计文档 |
| `event/sda_event.h` | 路由器公共头文件 |
| `event/sda_event.c` | 路由器实现 |
| `event/sda_event_entry.c` | SDK 回调入口 + 初始化 + 接管逻辑 |
| `event/sda_action.h` | 动作链公共头文件（集中声明） |
| `event/sda_action.c` | 动作链实现 |
| `event/sda_event_types.h` | 标准事件数据类型定义 |
| `event/sda_event_sim.h` | NO_HW 模拟器公共头文件 |
| `event/sda_event_sim.c` | NO_HW 模拟器实现 |
| `event/CMakeLists.txt` | 编译配置 |
| `intf/intf_event.h` | intf 事件 handler 声明 |
| `intf/intf_event.c` | intf 事件 handler 实现 + 注册函数 + NO_HW 模拟生成器 |
| `intf/intf_action.c` | intf 动作链定义 |

### 12.2 修改文件

| 文件 | 修改内容 |
|------|----------|
| `include/sda_core.h` | 添加 `g_sda_cb_ctx` 声明，include event 头文件 |
| `init/sdk_init.c` | `fmInitialize` 改为轻量 bootstrap handler |
| `main.c` | `sw_init_fns[]` 增加 `sda_event_init_and_takeover` 和动作链 init 调用；事件循环按 SDA_NO_HW 分支 |
| `CMakeLists.txt` | 添加 event 子目录 |

### 12.3 不需要修改的文件

| 文件 | 原因 |
|------|------|
| `intf/intf_api.h/.c` | 接口轮询上报逻辑不变 |
| `intf/intf_cb.h/.c` | Redis 回调注册方式不变 |
| `sdk/` | SDK 头文件不动 |

---

## 13. 扩展指南

新增一个模块并支持事件处理：

```
Step 1: 创建 <module>/<module>_event.h
        → 用 SDA_EVENT_HANDLER 宏声明事件 handler

Step 2: 创建 <module>/<module>_event.c
        → 实现事件 handler，提供 <module>_event_register()

Step 3: 如需提供动作链：
        → 在 event/sda_action.h 中声明 extern sda_action_chain_t
        → 创建 <module>/<module>_action.c
        → 实现 <module>_action_init()，初始化动作链

Step 4: 如需订阅其他模块的动作链：
        → 在自身 _action_init() 中调用 sda_action_chain_add()

Step 5: 在 event/sda_event_entry.c 的 sda_event_init() 中
        调用 <module>_event_register(&g_router)

Step 6: 在 main.c 的 Phase 2 对应位置调用 <module>_action_init()

Step 7: (NO_HW) 如需模拟事件：
        → 在 <module>_event.c 中实现 sda_sim_event_fn
        → 提供 <module>_sim_register_all()，调用 sda_sim_register()
        → 在 main.c 的 sda_sim_run 之前调用 <module>_sim_register_all()
```
