# lite_switch 开发进度

> 最后更新：2026-06-08

## 1. 项目概述

基于 Intel FM10840 Switch Chip 的交换机软件框架，采用 SONiC 风格分层松耦合架构，通过 **Redis + Protobuf** 进行进程间通信。

```
┌──────────────────────────────────────────────────┐
│  1.UI (CLI / Web)          ← 待开发              │
│  CONFIG_DB (Redis DB#0)                          │
├──────────────────────────────────────────────────┤
│  2.PI (Protocol Control)   ← 待开发              │
│  STATE_DB (Redis DB#1)                           │
├──────────────────────────────────────────────────┤
│  3.PD (Packet Processing)                        │
│    ├─ 3.test  (Pub/Sub 验证)  ✅                │
│    ├─ 4.SDA  (SDK 透传)      ✅                │
│    ├─ 1.Bridge (硬件抽象)    待开发              │
│    └─ 2.Route  (路由)        待开发              │
│  FWD_DB (Redis DB#2)                             │
├──────────────────────────────────────────────────┤
│  4.SDA (Data Plane — SDK pass-through)           │
└──────────────────────────────────────────────────┘
```

| 技术栈 | 选型 |
|--------|------|
| 消息总线 | Redis 8.0 (KV + Pub/Sub) |
| 序列化 | protobuf-c 1.5 |
| Redis 客户端 | hiredis 1.2 |
| 构建 | CMake 3.10+ / GCC |
| 编译器 | GCC |

---

## 2. 已完成模块

### 2.1 middleware — 中间件层 ✅

**路径**: `middleware/`

统一的 Redis 操作封装，组件不直接依赖 hiredis/protobuf-c 底层 API。

| API | 功能 |
|-----|------|
| `mw_connect` / `mw_disconnect` / `mw_select_db` | 连接管理 |
| `mw_set` / `mw_get` / `mw_del` | 裸二进制 KV |
| `mw_set_message` / `mw_get_message` / `mw_del_message` | Protobuf 消息操作，自动派生 `s_{package}/` 前缀 |
| `mw_hset` / `mw_hget` / `mw_hgetall` / `mw_hmupdate` | Hash 操作 |
| `mw_subscribe_keys` + `mw_poll` | Pub/Sub 回调模型：懒订阅 PSUBSCRIBE，自动反序列化 |

**entry->index 自动重建**：`mw_poll_one` 在反序列化 value 后，自动从 Redis key 二进制解析 key 消息并覆盖 `entry->index`。因此生产端写 value 时无需填充 index，消费端回调中 `entry->index` 始终与 key 对齐。

### 2.2 protocol — 协议层 ✅

**路径**: `protocol/`

- CMake 自动扫描 `proto/` 下所有 `.proto`，生成 `.pb-c.c/.h`，编译为 `liblight_protocol.a`
- 已有定义：

| Proto | 说明 |
|-------|------|
| `test/test.proto` | LightTestKey / LightTestEntry（Pub/Sub 验证用） |
| `PD/interface/interface.proto` | PdInterfaceKey / PdInterfaceEntry + 3 个枚举 + PdInterfaceLaneInfo |

- **redis-cli-proto** (`protocol/cli/`)：基于 Redis 8.0.5 的 proto 感知 CLI，零硬编码反射引擎

| 功能 | 说明 |
|------|------|
| KEYS / SCAN | 二进制 key ↔ 人类可读路径 |
| GET | proto 反射多行打印（无冗余 key 行，跳 index 字段，字符串无引号） |
| SET | key + value 均从人类可读编码为 proto 二进制 |
| MONITOR | 紧凑单行：`"key" "f1=v1,f2=v2"`（逗号无空格，value 有引号） |

### 2.3 3.test — Pub/Sub 验证模块 ✅

**路径**: `modules/3.PD/3.test/`

| 组件 | 功能 |
|------|------|
| `publisher` | SET → sleep 3s → DEL |
| `receiver` | 订阅 keyspace → 回调打印 → del 时退出 |

已验证 middleware 回调模型数据流完整可用。

### 2.4 4.SDA — SDK 透传守护进程 ✅

**路径**: `modules/3.PD/4.SDA/`

#### 架构

```
main.c（三阶段初始化框架）
  │
  ├─ Phase 1: hw_init_fns[]    — 硬件初始化
  │     └─ sdk_init_all        交换机上电、SDK 驱动加载
  │
  ├─ Phase 2: sw_init_fns[]    — 软件初始化
  │     ├─ sda_sw_redis_connect  Redis 建联
  │     └─ interface_cb_register 接口事件回调注册
  │
  ├─ Phase 3: custom_init_fns[] — 自定义任务
  │     └─ sda_interface_poll_all  接口信息上报
  │
  └─ 事件循环: mw_poll → on_interface_event → SDK 查询 → Redis 写回
```

#### 新增模块方式

只需在 `main.c` 的对应数组中挂载函数指针：

```c
// Phase 1 — 硬件初始化
static const hw_init_fn_t hw_init_fns[] = {
    sdk_init_all,
    NULL
};

// Phase 2 — 软件初始化
static const sw_init_fn_t sw_init_fns[] = {
    sda_sw_redis_connect,
    interface_cb_register,     // ← 新增模块的回调注册函数加这里
    NULL
};

// Phase 3 — 自定义任务
static const custom_init_fn_t custom_init_fns[] = {
    sda_interface_poll_all,    // ← 新增模块的初始化任务加这里
    NULL
};
```

#### interface 子模块

**路径**: `src/api/interface/`

| 文件 | 职责 |
|------|------|
| `interface_api.h` | `sda_interface_fill_entry` / `sda_interface_poll_all` |
| `interface_api.c` | SDK 调用 + 直接 cast 填 proto entry（无 switch-case） |
| `interface_cb.h` | `interface_cb_register` |
| `interface_cb.c` | keyspace 回调：收到查询 → SDK 填 entry → Redis 写回 |

#### Proto 枚举（与 SDK 值一一对应，直接 cast）

| 枚举 | SDK 值来源 | 示例 |
|------|-----------|------|
| `PdInterfaceSpeed` | `fm_uint32` Mbps | `(PdInterfaceSpeed)10000` = 10G |
| `PdInterfaceAdminMode` | `fm_int` mode | `(PdInterfaceAdminMode)1` = ADMIN_DOWN |
| `PdInterfaceLinkState` | `fm_int` state | `(PdInterfaceLinkState)5` = LINK_DOWN |
| `PdInterfaceLaneInfo` | `fm_int[4]` info | rx_pll / tx_pll / signal / align / auto_det / mismatch |

---

## 3. 编译

```bash
./build.sh          # Debug 编译（默认）
./build.sh release  # Release 编译
./build.sh clean    # 清理所有产物
```

产物：
```
modules/build/bin/
├── publisher
├── receiver
├── sda
└── redis-cli-proto
```

`SDA_NO_HW=ON`（默认）可脱离硬件编译。

---

## 4. 待开发模块

| 优先级 | 模块 | 路径 | 职责 |
|--------|------|------|------|
| P1 | Bridge (fwd) | `modules/3.PD/1.Bridge/` | 硬件资源抽象，状态缓存，操作编排 |
| P2 | Route | `modules/3.PD/2.Route/` | 路由表管理 |
| P3 | PI (ctrl) | `modules/2.PI/` | VLAN/LAG/STP 协议控制逻辑 |
| P4 | CLI | `modules/1.UI/1.CLI/` | 文本命令 → Protobuf，结果展示 |
| P5 | Web | `modules/1.UI/2.Web/` | Web 管理界面 |

---

## 5. 开发约定

- 所有模块通过 `middleware.h` 访问 Redis，不直接依赖 hiredis/protobuf-c
- 回调签名统一：`void cb(const char *event, void *value, void *user_data)`
- value 使用后必须调用 `mw_free_message()` 释放
- CMakeLists.txt 使用 `add_light_component(name src.c ...)` 宏
- Proto 枚举值与 SDK 原始值保持一致，直接 cast 赋值
- SDA 只通过 proto 通信，不做字符串转换
- **生产端写 value 时无需填充 `entry->index`**，中间件 `mw_poll_one` 会从 Redis key 自动解析并覆盖；消费端回调中 `entry->index` 始终有效
- Proto key 格式统一：`s_{package}/<binary_key>`，由 `mw_make_prefix` 自动派生
- `redis-cli-proto` 支持人类可读 proto 命令，详见 [`protocol/README.md`](protocol/README.md)
