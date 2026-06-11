# lite_switch 项目开发大纲

> 最后更新：2026-06-11

## 1. 项目概述

### 1.1 项目目标

`lite_switch` 是基于 Intel FM10840 Switch Chip 的交换机软件框架，分层松耦合架构，Redis + Protobuf 进程间通信。

1. **分层解耦**：每层独立进程，职责清晰，可独立开发、调试、替换
2. **数据驱动**：Redis 为唯一消息总线，Protobuf 为统一数据格式
3. **硬件抽象**：`4.SDA` 为唯一硬件相关模块，其余各层与芯片型号无关
4. **可组合**：`./build.sh` 一键编译，单模块独立构建支持快速迭代

### 1.2 技术栈

| 组件 | 选型 | 用途 |
|------|------|------|
| 消息总线 | Redis 8.0 | KV 存储 + Pub/Sub 事件通知 |
| 数据格式 | protobuf-c 1.5 | 跨语言序列化，强类型校验 |
| Redis 客户端 | hiredis 1.2 (submodule) | C 语言 Redis 驱动 |
| 日志 | zlog 1.2.18 (submodule) | 结构化日志，按进程分目录 |
| 构建系统 | CMake 3.10+ | 全量编译 / 单模块独立构建 |
| 编译器 | GCC | C 语言编译 |

### 1.3 架构设计（SONiC 对齐）

```
┌──────────────────────────────────────────────────┐
│  1.UI (CLI / Web)           ← 用户交互            │
│  CONFIG_DB (Redis DB#0)                          │
├──────────────────────────────────────────────────┤
│  2.PI (Protocol Control)    ← 协议控制            │
│  STATE_DB (Redis DB#1)                           │
├──────────────────────────────────────────────────┤
│  3.PD (Packet Processing)   ← 数据处理/硬件抽象   │
│  FWD_DB (Redis DB#2)                             │
├──────────────────────────────────────────────────┤
│  4.SDA (SDK Pass-through)   ← SDK 透传            │
└──────────────────────────────────────────────────┘
```

---

## 2. 工程目录结构

```
lite_switch/
├── README.md
├── PROJECT_OUTLINE.md          # 项目大纲（本文件）
├── DEVELOPMENT.md              # 开发进度跟踪
├── build.sh                    # 编译 / 清理 控制脚本
├── .gitignore
├── .gitmodules                 # git submodule 定义
├── cmake/                      # CMake 辅助模块
│   ├── comp_base.cmake         # 单组件编译宏（自动链接 middleware + protocol）
│   └── aggregate.cmake         # 模块聚合器（自动发现子目录并编译）
├── deps/                       # git submodule 外部依赖
│   ├── hiredis/                # Redis C 客户端，静态编译
│   └── zlog/                   # 日志库，静态编译
├── middleware/                  # 中间件层
│   ├── CMakeLists.txt          # 编译为 libmiddleware.so
│   ├── include/
│   │   └── middleware.h        # 公开 API：Redis 连接/KV/Pub/Sub/scan/日志
│   └── src/
│       └── middleware.c        # 基于 hiredis + protobuf-c + zlog 实现
├── protocol/                   # Protobuf 定义与编译
│   ├── CMakeLists.txt          # proto → .pb-c.c/.h，编译为 liblight_protocol.a
│   ├── README.md               # 模块使用说明
│   ├── gen_proto_registry.sh   # 生成 cli/proto_registry.c
│   ├── proto/                  # .proto 源文件
│   │   ├── test/test.proto
│   │   └── PD/interface/interface.proto
│   ├── include/                # 编译产物（gitignored）
│   │   ├── test/test.pb-c.{c,h}
│   │   └── PD/interface/interface.pb-c.{c,h}
│   └── cli/                    # redis-cli-proto 构建
│       ├── build_redis_cli.sh  # 克隆 Redis 源码 → patch → 编译
│       ├── patch_redis_cli.py  # Python patcher
│       ├── cli_proto.{c,h}     # protobuf 反射引擎
│       └── proto_registry.c    # 自动生成（gitignored）
├── modules/                    # 各层模块
│   ├── CMakeLists.txt          # 顶层构建入口（aggregate.cmake）
│   ├── 1.UI/                   # 用户接口层
│   │   ├── CMakeLists.txt
│   │   └── 1.Web/              # Web 状态页面 ✅
│   │       ├── CMakeLists.txt
│   │       └── main.c          # HTTP + SSE + Redis Pub/Sub
│   ├── 2.PI/                   # 协议控制层（待开发）
│   └── 3.PD/                   # 数据处理层
│       ├── CMakeLists.txt
│       ├── 1.Bridge/           # 硬件抽象（待开发）
│       ├── 2.Route/            # 路由管理（待开发）
│       ├── 3.test/             # 测试模块
│       │   ├── CMakeLists.txt
│       │   ├── publisher/      # 注入 PdInterface 测试数据
│       │   ├── receiver/       # Pub/Sub 回调验证
│       │   └── scanner/        # mw_scan 存量发现 ★
│       └── 4.SDA/              # SDK 透传守护进程 ✅
│           ├── CMakeLists.txt
│           ├── main.c           # 三阶段初始化 + 事件循环
│           ├── init/sdk_init.c  # SDK 初始化
│           └── intf/            # interface 子模块（API + Callback）
└── scripts/                    # 构建和运维脚本
    ├── install_deps.sh          # 安装系统依赖（protobuf-c）
    └── check_deps.sh            # 校验依赖就绪状态
```

---

## 3. build.sh — 工程编译控制脚本

### 3.1 用法

```bash
./build.sh          # Debug 编译（默认）
./build.sh release  # Release 编译
./build.sh clean    # 清除所有编译产物
```

### 3.2 构建流程

```
./build.sh
  ├─ git submodule update --init      # 拉取 deps/
  ├─ cmake -S modules/ -B modules/build
  │   └─ aggregate.cmake 遍历所有子目录
  │       ├─ 1.UI/1.Web/     → switch-web
  │       └─ 3.PD/
  │           ├─ 3.test/publisher  → publisher
  │           ├─ 3.test/receiver   → receiver
  │           ├─ 3.test/scanner    → scanner
  │           └─ 4.SDA             → sda
  ├─ cmake --build modules/build -- -j$(nproc)
  └─ 编译 redis-cli-proto（从 Redis 8.0.5 源码）
```

**产物位置：**

| 类型 | 路径 |
|------|------|
| 可执行文件 | `modules/build/bin/publisher`, `receiver`, `scanner`, `sda`, `switch-web` |
| redis-cli-proto | `modules/build/bin/redis-cli-proto` |
| 动态库 | `modules/build/lib/libmiddleware.so` |
| 静态库 | `modules/build/protocol/liblight_protocol.a` |
| compile_commands.json | 工程根目录（clangd LSP 用） |

### 3.3 clean 清除内容

```
modules/build/           # 编译产物
protocol/build/          # protocol 独立编译产物
middleware/build/        # middleware 独立编译产物
modules/.cache/          # CMake 缓存
.cache/                  # CMake 缓存
compile_commands.json    # LSP 索引
protocol/include/        # protobuf-c 生成的 .pb-c.c/.h
/tmp/redis-src/          # redis-cli-proto 中间产物
protocol/cli/proto_registry.c  # 自动生成的 registry
```

---

## 4. middleware — 中间件层

### 4.1 API 总览

| 类别 | 函数 | 功能 |
|------|------|------|
| 连接 | `mw_connect` / `mw_disconnect` / `mw_select_db` | Redis 连接管理（mw_connect 自动开启 keyspace 通知） |
| KV | `mw_set` / `mw_get` / `mw_del` | 裸二进制 KV 操作 |
| Proto | `mw_set_message` / `mw_get_message` / `mw_del_message` | Protobuf 消息操作，自动派生 `s_{package}/` key 前缀 |
| Hash | `mw_hset` / `mw_hget` / `mw_hgetall` / `mw_hmupdate` | Hash 表操作 |
| Pub/Sub | `mw_subscribe_keys` + `mw_poll` | 回调模型：懒订阅 PSUBSCRIBE，自动反序列化 |
| Scan | `mw_scan` | ★ 存量发现：SCAN → 入队，mw_poll 统一消费 |
| 日志 | `mw_log_init(proc_name)` | ★ zlog 初始化，写入 `/tmp/lite-switch/<proc>/` |
| FD | `mw_get_sub_fd` | ★ 获取 Pub/Sub fd（select/epoll 多路复用） |

### 4.2 关键设计

- **entry->index 自动重建**：mw_poll_one / mw_scan 从 Redis key 二进制解析 proto key 并覆盖 entry->index。生产端写 value 无需填充 index。
- **懒订阅**：首次 mw_poll 调用时自动创建 sub_conn + PSUBSCRIBE
- **scan + poll 协作**：mw_scan 仅入队，mw_poll 统一消费（先 pending 队列，再 Pub/Sub）
- **依赖源码化**：hiredis + zlog 为 git submodule，静态编译进 middleware

---

## 5. protocol — Protobuf 定义与编译

### 5.1 Proto 定义

| Proto | 说明 |
|-------|------|
| `test/test.proto` | LightTestKey / LightTestEntry（Pub/Sub 验证用） |
| `PD/interface/interface.proto` | PdInterfaceKey / PdInterfaceEntry + 3 个枚举 + PdInterfaceLaneInfo |

### 5.2 新增 Proto 步骤

1. 在 `proto/<type>/` 下创建 `xxx.proto`（proto3 语法，需声明 `package <pkg>;`）
2. 重新 `./build.sh` 即可，CMake 自动发现并生成 `.pb-c.c/.h`

Proto key 由 middleware 自动管理，格式：`s_{package}/ + packed(key_msg)`

### 5.3 redis-cli-proto

基于 Redis 8.0.5 源码编译的 proto 感知 CLI：

| 功能 | 说明 |
|------|------|
| KEYS / SCAN | 二进制 key ↔ 人类可读路径 |
| GET | proto 反射多行打印 |
| SET | key + value 人类可读编码为 proto 二进制 |
| MONITOR | 紧凑格式 + 文本筛选（`MONITOR s_pd_interface`）+ 支持 SET/GET/DEL/HSET/HGET/PUBLISH |

---

## 6. cmake 构建辅助模块

### 6.1 comp_base.cmake

单组件编译宏，自动引入 middleware + protocol + deps：

```cmake
add_light_component(my_module main.c)
# 等价于:
#   add_executable(my_module main.c)
#   target_link_libraries(my_module middleware light_protocol)
#   产物 → build/bin/
```

### 6.2 aggregate.cmake

通用模块聚合器：自动遍历当前目录下所有含 CMakeLists.txt 的子目录并编译。

---

## 7. 已实现模块

| 模块 | 路径 | 功能 |
|------|------|------|
| publisher | `modules/3.PD/3.test/publisher/` | 注入 PdInterface 测试数据（使用 mw_set_message） |
| receiver | `modules/3.PD/3.test/receiver/` | 订阅 keyspace → 回调打印 |
| scanner | `modules/3.PD/3.test/scanner/` | ★ mw_scan 存量发现 → subscribe → mw_poll 统一消费 |
| sda | `modules/3.PD/4.SDA/` | SDK 透传守护进程，三阶段初始化框架 |
| switch-web | `modules/1.UI/1.Web/` | ★ HTTP + SSE 端口状态页面（端口 8080） |
| redis-cli-proto | `modules/build/bin/` | Proto 感知 CLI 诊断工具 |

---

## 8. 待开发模块

| 优先级 | 模块 | 路径 | 职责 |
|--------|------|------|------|
| P1 | Bridge | `modules/3.PD/1.Bridge/` | 硬件资源抽象，状态缓存，操作编排 |
| P2 | Route | `modules/3.PD/2.Route/` | 路由表管理 |
| P3 | PI | `modules/2.PI/` | VLAN/LAG/STP 协议控制逻辑 |
| P4 | CLI | `modules/1.UI/1.CLI/` | 文本命令 → Protobuf，结果展示 |

---

## 9. 开发约定

### 9.1 Proto 规范

- 每个业务资源定义一对 key + entry message
- key message 内嵌所有定位字段
- entry message 的 `index` 字段指向 key message
- 必须声明 `package xxx;`

### 9.2 组件编码规范

- 组件只通过 `middleware.h` 访问 Redis，不直接使用 hiredis/protobuf-c
- 回调签名统一：`void cb(const char *event, void *value, void *user_data)`
- value 使用后必须调用 `mw_free_message()` 释放
- CMakeLists.txt 使用 `add_light_component(name src.c ...)` 宏
- 进程启动时调用 `mw_log_init("proc_name")`，之后用 `dzlog_*` 宏写日志

### 9.3 构建约定

- 全量编译：`./build.sh`
- 清理产物：`./build.sh clean`
- 不向版本库提交：`modules/build/`, `protocol/include/`, `.cache/`, `compile_commands.json`, `dump.rdb`
