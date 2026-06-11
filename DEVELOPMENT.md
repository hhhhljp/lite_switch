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
│    │   └─ scanner    (mw_scan 存量发现)  ★新增   │
│    ├─ 4.SDA  (SDK 透传)      ✅                 │
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
| `mw_scan` | ★ 存量发现：串行 SCAN → 入队，由 mw_poll 统一消费 |
| `mw_log_init(proc_name)` | ★ zlog 初始化，日志写入 /tmp/lite-switch/<proc>/ |
| `mw_get_sub_fd` | ★ 获取 Pub/Sub fd（用于 select/epoll 多路复用） |

**entry->index 自动重建**：`mw_poll_one` / `mw_scan` 在反序列化 value 后，自动从 Redis key 二进制解析 key 消息并覆盖 `entry->index`。

**依赖零外部 apt**：hiredis 和 zlog 均为 git submodule（`deps/`），编译为静态库链接，不产生 `.so` 运行时依赖。

### 2.2 protocol — 协议层 ✅

**路径**: `protocol/`

- CMake 自动扫描 `proto/` 下所有 `.proto`，生成 `.pb-c.c/.h`，编译为 `liblight_protocol.a`
- 已有定义：

| Proto | 说明 |
|-------|------|
| `test/test.proto` | LightTestKey / LightTestEntry（Pub/Sub 验证用） |
| `PD/interface/interface.proto` | PdInterfaceKey / PdInterfaceEntry + 3 个枚举 + PdInterfaceLaneInfo |

- **redis-cli-proto** (`protocol/cli/`)：基于 Redis 8.0.5 源码编译的 proto 感知 CLI

| 功能 | 说明 |
|------|------|
| KEYS / SCAN | 二进制 key ↔ 人类可读路径 |
| GET | proto 反射多行打印 |
| SET | key + value 均从人类可读编码为 proto 二进制 |
| MONITOR | 紧凑格式 + 文本筛选过滤器（`MONITOR s_pd_interface`）+ 支持 SET/GET/DEL/HSET/HGET/PUBLISH |

### 2.3 3.test — 测试模块 ✅

**路径**: `modules/3.PD/3.test/`

| 组件 | 功能 |
|------|------|
| `publisher` | 用 mw_set_message 注入 PdInterface 测试数据 |
| `receiver` | 订阅 keyspace → 回调打印 → del 时退出 |
| `scanner` | ★ mw_scan 存量发现 → subscribe → mw_poll 统一消费 |

### 2.4 4.SDA — SDK 透传守护进程 ✅

**路径**: `modules/3.PD/4.SDA/`

三阶段初始化 + mw_poll 事件循环。`SDA_NO_HW=ON` 可脱离硬件编译。

| 子模块 | 路径 | 职责 |
|--------|------|------|
| interface API | `intf/intf_api.c` | SDK 调用填充 proto entry → Redis 写回 |
| interface CB | `intf/intf_cb.c` | keyspace 回调注册 |

### 2.5 1.Web — Web 状态页面 ✅ ★新增

**路径**: `modules/1.UI/1.Web/`

| 特性 | 实现 |
|------|------|
| HTTP 服务 | 原生 POSIX socket + select() 多路复用（零外部依赖） |
| 页面 | 内嵌 HTML/CSS/JS，卡片式设计，统计面板 |
| 实时更新 | SSE 推送，mw_scan 存量发现 + mw_subscribe_keys 增量订阅 |
| 日志 | zlog 写入 /tmp/lite-switch/switch-web/ |

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
├── scanner          ★新增
├── sda
├── switch-web       ★新增
└── redis-cli-proto
```

---

## 4. 待开发模块

| 优先级 | 模块 | 路径 | 职责 |
|--------|------|------|------|
| P1 | Bridge (fwd) | `modules/3.PD/1.Bridge/` | 硬件资源抽象，状态缓存，操作编排 |
| P2 | Route | `modules/3.PD/2.Route/` | 路由表管理 |
| P3 | PI (ctrl) | `modules/2.PI/` | VLAN/LAG/STP 协议控制逻辑 |
| P4 | CLI | `modules/1.UI/1.CLI/` | 文本命令 → Protobuf，结果展示 |

---

## 5. 开发约定

- 所有模块通过 `middleware.h` 访问 Redis，不直接依赖 hiredis/protobuf-c
- 回调签名统一：`void cb(const char *event, void *value, void *user_data)`
- value 使用后必须调用 `mw_free_message()` 释放
- CMakeLists.txt 使用 `add_light_component(name src.c ...)` 宏
- Proto 枚举值与 SDK 原始值保持一致，直接 cast 赋值
- SDA 只通过 proto 通信，不做字符串转换
- **生产端写 value 时无需填充 `entry->index`**，中间件自动从 Redis key 重建
- Proto key 格式统一：`s_{package}/<binary_key>`，由 `mw_make_prefix` 自动派生
- 外部依赖源码化：hiredis/zlog 使用 git submodule，Redis 源码编译 redis-cli-proto
- 日志使用 zlog：进程启动时调用 `mw_log_init("proc_name")`，之后用 `dzlog_*` 宏
