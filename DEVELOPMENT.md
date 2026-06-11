# lite_switch 开发进度

> 最后更新：2026-06-11

## 1. 项目概述

基于 Intel FM10840 Switch Chip 的交换机软件框架，采用 SONiC 风格分层松耦合架构，通过 **Redis + Protobuf** 进行进程间通信。

```
┌──────────────────────────────────────────────────┐
│  1.UI (CLI / Web)                                │
│    ├─ 1.Web  (端口状态 Web)   ✅                 │
│    └─ 1.CLI  (命令行)         待开发             │
│  CONFIG_DB (Redis DB#0)                          │
├──────────────────────────────────────────────────┤
│  2.PI (Protocol Control)    ← 待开发             │
│  STATE_DB (Redis DB#1)                           │
├──────────────────────────────────────────────────┤
│  3.PD (Packet Processing)                        │
│    ├─ 3.test  (Pub/Sub 验证)  ✅                 │
│    │   ├─ publisher  (注入接口数据)              │
│    │   ├─ receiver   (订阅回调验证)              │
│    │   └─ scanner    (mw_scan 存量发现)          │
│    ├─ 4.SDA  (SDK 透传)      ✅                 │
│    │   ├─ intf   (接口状态同步)                  │
│    │   ├─ event  (事件路由 + 动作链) ★新增        │
│    │   └─ sim    (NO_HW 模拟器)  ★新增           │
│    ├─ 1.Bridge (硬件抽象)    待开发              │
│    └─ 2.Route  (路由)        待开发              │
│  FWD_DB (Redis DB#2)                             │
└──────────────────────────────────────────────────┘
```

| 技术栈 | 选型 |
|--------|------|
| 消息总线 | Redis 8.0 (KV + Pub/Sub) |
| 序列化 | protobuf-c 1.5 |
| Redis 客户端 | hiredis 1.2 (git submodule) |
| 日志 | zlog 1.2.18 (git submodule) |
| 构建 | CMake 3.10+ / GCC |

---

## 2. 已完成模块

### 2.1 middleware — 中间件层 ✅

**路径**: `middleware/`

统一的 Redis 操作封装，组件不直接依赖 hiredis/protobuf-c 底层 API。

| API | 功能 |
|-----|------|
| `mw_connect` / `mw_disconnect` / `mw_select_db` | 连接管理（mw_connect 自动 CONFIG SET notify-keyspace-events） |
| `mw_set` / `mw_get` / `mw_del` | 裸二进制 KV |
| `mw_set_message` / `mw_get_message` / `mw_del_message` | Protobuf 消息操作，自动派生 `s_{package}/` 前缀 |
| `mw_hset` / `mw_hget` / `mw_hgetall` / `mw_hmupdate` | Hash 操作 |
| `mw_subscribe_keys` + `mw_poll` | Pub/Sub 回调模型：懒订阅 PSUBSCRIBE，自动反序列化 |
| `mw_scan` | 存量发现：串行 SCAN → 入队，由 mw_poll 统一消费 |
| `mw_log_init(proc_name)` | zlog 初始化，日志写入 /tmp/lite-switch/<proc>/ |
| `mw_get_sub_fd` | 获取 Pub/Sub fd（用于 select/epoll 多路复用） |

entry->index 自动重建，依赖零外部 apt。

### 2.2 protocol — 协议层 ✅

**路径**: `protocol/`

| Proto | 说明 |
|-------|------|
| `test/test.proto` | LightTestKey / LightTestEntry（Pub/Sub 验证用） |
| `PD/interface/interface.proto` | PdInterfaceKey / PdInterfaceEntry + 3 个枚举 + PdInterfaceLaneInfo |

redis-cli-proto: 基于 Redis 8.0.5 源码编译的 proto 感知 CLI（KEYS/GET/SET/MONITOR）。

### 2.3 3.test — 测试模块 ✅

**路径**: `modules/3.PD/3.test/`

| 组件 | 功能 |
|------|------|
| `publisher` | 用 mw_set_message 注入 PdInterface 测试数据 |
| `receiver` | 订阅 keyspace → 回调打印 |
| `scanner` | mw_scan 存量发现 → subscribe → mw_poll 统一消费 |

### 2.4 4.SDA — SDK 透传守护进程 ✅

**路径**: `modules/3.PD/4.SDA/`

三阶段初始化框架。`SDA_NO_HW=ON` 可脱离硬件编译运行。

| 子模块 | 路径 | 职责 |
|--------|------|------|
| init | `init/sdk_init.c` | SDK 初始化（fmOSInitialize → fmInitialize → fmSetSwitchState） |
| interface API | `intf/intf_api.c` | SDK 调用填充 proto entry → Redis 写回；NO_HW 注入模拟数据 |
| interface CB | `intf/intf_cb.c` | keyspace 回调注册（骨架，待接入） |
| **event 路由** ★ | `event/sda_event.h` | 事件处理函数签名 + `SDA_EVENT_HANDLER` 宏 |
| **NO_HW 模拟** ★ | `event/sda_event_sim.h/.c` | 周期定时器框架，注册生成器 → `sda_sim_run` 循环触发 |

**NO_HW 模拟框架** ★新增：

| API | 功能 |
|-----|------|
| `sda_sim_register(fn)` | 注册模拟事件生成器 |
| `sda_sim_run(ctx, interval_ms, &running)` | 启动模拟循环（阻塞），检测 `running` 标志支持信号优雅退出 |

**模拟链路翻转测试**（`intf/intf_event.c`）：

| 函数 | 功能 |
|------|------|
| `intf_sim_link_toggle(ctx)` | 每秒翻转 port 0 的 link_state（UP ↔ DOWN），写 Redis |
| `intf_sim_register_all()` | 注册所有 intf 模拟生成器 |

### 2.5 1.Web — Web 状态页面 ✅

**路径**: `modules/1.UI/1.Web/`

| 特性 | 实现 |
|------|------|
| HTTP 服务 | 原生 POSIX socket + select() 多路复用 |
| 页面 | 内嵌 HTML/CSS/JS，卡片式设计，统计面板 |
| 实时更新 | SSE 推送，mw_scan 存量发现 + mw_subscribe_keys 增量订阅 |
| 静默渲染 | 数据变化时直接更新 DOM，无动画闪烁 |
| 日志 | zlog 写入 /tmp/lite-switch/switch-web/ |

---

## 3. SDA 事件处理设计 ★

完整设计文档见 `modules/3.PD/4.SDA/event/SDA_EVENT_DESIGN.md`。核心要点：

### 3.1 两套事件机制

| 机制 | 路径 | 时延 | 适用场景 |
|------|------|------|----------|
| **事件路由** | SDK → event 模块 → 子模块 → Redis → 上层 | 毫秒级 | 状态同步、Web 推送 |
| **动作链** | SDK → event 模块 → 子模块 → 直调目标函数 | 微秒级 | ECMP 快切、MAC 表联动 |

### 3.2 初始化顺序（消除竞态）

```
Phase 1: （空）
Phase 2: Redis连接 → 事件框架 → 动作链 → callback注册 → SDK初始化(fmInitialize直接注册)
Phase 3: 接口轮询上报 + 模拟器注册启动（NO_HW）
```

> ⚠️ 此初始化顺序尚未在真实硬件上验证。

---

## 4. 编译

```bash
./build.sh          # 仿真环境 Debug 编译（SDA_NO_HW=ON）
./build.sh hw       # 真实硬件 Debug 编译（SDA_NO_HW=OFF）
./build.sh release  # 仿真环境 Release 编译
./build.sh clean    # 清理所有产物
```

产物：
```
modules/build/bin/
├── publisher
├── receiver
├── scanner
├── sda                 ← 含事件模拟框架
├── switch-web          ← 静默渲染
└── redis-cli-proto
```

---

## 5. 待开发模块

| 优先级 | 模块 | 路径 | 职责 |
|--------|------|------|------|
| **P0** | SDA 实机验证 | `modules/3.PD/4.SDA/` | 新初始化流程 + 事件回调在真实 FM10840 上验证 |
| P1 | Bridge (fwd) | `modules/3.PD/1.Bridge/` | 硬件资源抽象，状态缓存，操作编排 |
| P2 | Route | `modules/3.PD/2.Route/` | 路由表管理 |
| P3 | PI (ctrl) | `modules/2.PI/` | VLAN/LAG/STP 协议控制逻辑 |
| P4 | CLI | `modules/1.UI/1.CLI/` | 文本命令 → Protobuf，结果展示 |

---

## 6. 开发约定

- 所有模块通过 `middleware.h` 访问 Redis，不直接依赖 hiredis/protobuf-c
- 回调签名统一：`void cb(const char *event, void *value, void *user_data)`
- value 使用后必须调用 `mw_free_message()` 释放
- CMakeLists.txt 使用 `add_light_component(name src.c ...)` 宏
- Proto 枚举值与 SDK 原始值保持一致，直接 cast 赋值
- 生产端写 value 时无需填充 `entry->index`，中间件自动从 Redis key 重建
- Proto key 格式统一：`s_{package}/<binary_key>`
- 日志使用 zlog：进程启动时调用 `mw_log_init("proc_name")`
- SDA 双 Redis 连接：主线程 `g_sda_ctx` + 回调线程 `g_sda_cb_ctx`（将来 HW 模式）
