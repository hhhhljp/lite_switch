# lite_switch

基于 Intel FM10840 Switch Chip 的交换机软件框架，采用分层松耦合架构，通过 Redis + Protobuf 进行进程间通信。

## 架构概览

```
┌──────────────────────────────────────────────────────┐
│  Management Plane    →    1.UI (CLI / Web)           │
│  CONFIG_DB           →    CONFIG_DB (Redis DB#0)     │
│  Control Plane       →    2.PI (protocol control)    │
│  APPL_DB             →    STATE_DB (Redis DB#1)      │
│  Orchestration Layer →    3.PD (hardware abstract)   │
│  ASIC_DB             →    FWD_DB (Redis DB#2)        │
│  Data Plane          →    4.SDA (SDK pass-through)   │
└──────────────────────────────────────────────────────┘
```

## 依赖

| 依赖 | 版本要求 | 用途 | 来源 |
|------|---------|------|------|
| CMake | >= 3.10 | 构建系统 | 系统 |
| GCC | >= 13 | C 编译器 | 系统 |
| Redis | >= 6.0 | 消息总线（KV + Pub/Sub） | 系统 |
| hiredis | 1.2.0 | Redis C 客户端 | **git submodule** (`deps/hiredis`) |
| zlog | 1.2.18 | 结构化日志 | **git submodule** (`deps/zlog`) |
| protobuf-c | >= 1.4 | Protobuf C 运行时 | 系统 (`apt install libprotobuf-c-dev protobuf-c-compiler`) |
| protoc-gen-c | >= 1.4 | .proto 编译器插件 | 系统 |

### 克隆 & 编译

```bash
git clone --recurse-submodules https://github.com/xxx/lite_switch.git
cd lite_switch
./scripts/install_deps.sh     # 安装系统依赖（protobuf-c）
./build.sh                     # 全量编译
```

### 校验依赖

```bash
./scripts/check_deps.sh
```

## 项目结构

```
lite_switch/
├── README.md
├── PROJECT_OUTLINE.md          # 项目开发大纲
├── DEVELOPMENT.md              # 开发进度跟踪
├── build.sh                    # 编译 / 清理控制脚本
├── cmake/                      # CMake 辅助模块
│   ├── comp_base.cmake         # 单组件编译宏
│   └── aggregate.cmake         # 模块聚合器
├── deps/                       # git submodule 外部依赖
│   ├── hiredis/                # Redis C 客户端 (BSD 3-Clause)
│   └── zlog/                   # 日志库 (Apache 2.0)
├── middleware/                  # 中间件层 (libmiddleware.so)
├── protocol/                   # Protobuf 定义与编译 (liblight_protocol.a)
└── modules/                    # 各层模块
    ├── 1.UI/1.Web/             # Web 状态页面 (switch-web)
    ├── 3.PD/
    │   ├── 3.test/             # 测试模块 (publisher/receiver/scanner)
    │   └── 4.SDA/              # SDK 透传守护进程 (sda)
    └── ...
```

## 编译

```bash
./build.sh          # Debug 编译（默认）
./build.sh release  # Release 编译
./build.sh clean    # 清理所有产物
```

产物：
```
modules/build/bin/
├── publisher           # 注入 PdInterface 测试数据
├── receiver            # Pub/Sub 回调验证
├── scanner             # mw_scan 存量发现
├── sda                 # SDK 透传守护进程
├── switch-web          # Web 状态页面 (端口 8080)
└── redis-cli-proto     # Proto 感知 CLI 诊断工具
```

## 添加新模块

1. 在 `modules/` 对应层级下创建子目录，放入 `CMakeLists.txt` 和源码
2. 子目录的 `CMakeLists.txt` 使用 `comp_base.cmake` 提供的 `add_light_component` 宏：

```cmake
cmake_minimum_required(VERSION 3.10)
project(my_module C)

set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
while(NOT EXISTS "${_d}/cmake/comp_base.cmake")
  get_filename_component(_d "${_d}" DIRECTORY)
endwhile()
include("${_d}/cmake/comp_base.cmake")
unset(_d)

add_light_component(my_module main.c)
```

## 添加新 Proto

1. 在 `protocol/proto/<type>/` 下创建 `xxx.proto`（proto3 语法）
2. 重新编译，CMake 会自动发现新文件并生成对应的 `.pb-c.c/.h`

## 技术栈

| 组件 | 选型 | 用途 |
|------|------|------|
| 消息总线 | Redis 8.0 | KV 存储 + Pub/Sub 事件通知 |
| 数据格式 | protobuf-c | 跨语言序列化，强类型校验 |
| Redis 客户端 | hiredis (submodule) | C 语言 Redis 驱动 |
| 日志 | zlog (submodule) | 结构化日志，按进程分目录 |
| 构建系统 | CMake 3.10+ | 全量编译 / 单模块独立构建 |
| 编译器 | GCC | C 语言编译 |
