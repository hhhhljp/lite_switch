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

| 依赖 | 版本要求 | 用途 |
|------|---------|------|
| CMake | >= 3.10 | 构建系统 |
| GCC | >= 13 | C 编译器 |
| Redis | >= 6.0 | 消息总线（KV 存储 + Pub/Sub） |
| hiredis | >= 1.0 | C Redis 客户端库 |
| protobuf-c | >= 1.4 | Protobuf C 运行时库 |
| protobuf-compiler (protoc) | >= 3.0 | .proto 编译器 |
| protoc-gen-c | >= 1.4 | protobuf-c 代码生成插件 |
| pkg-config | 任意 | 依赖查找 |

### 安装依赖

```bash
sudo ./scripts/install_deps.sh
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
├── build.sh                    # 编译 / 清理控制脚本
├── cmake/
│   ├── comp_base.cmake         # 单组件编译宏（自动链接 middleware + protocol）
│   └── aggregate.cmake         # 模块聚合器（自动发现子目录并编译）
├── middleware/                  # 中间件层 (libmiddleware.so)
│   ├── include/middleware.h    # 公开 API：Redis 连接、KV 操作、Pub/Sub 发布/订阅
│   └── src/middleware.c        # 基于 hiredis + protobuf-c 实现
├── protocol/                   # Protobuf 定义与编译 (liblight_protocol.a)
│   ├── proto/                  # .proto 源文件
│   └── include/                # 编译产物（自动生成）
└── modules/                    # 各层模块
    ├── 1.UI/                   # 用户接口层（待实现）
    ├── 2.PI/                   # 协议控制层（待实现）
    ├── 3.PD/                   # 数据处理层
    │   ├── 3.test/             # Pub/Sub 回调模型验证
    │   │   ├── publisher/      # SET → sleep → DEL
    │   │   └── receiver/       # 订阅 keyspace → 回调打印
    │   └── 4.SDA/              # SDK 透传层（待实现）
    ├── 4.SDA/                  # 硬件适配层（待实现）
    └── CMakeLists.txt          # 顶层构建入口
```

## 编译

### 全量编译

```bash
# Debug 编译（默认）
./build.sh

# 或显式指定
./build.sh build

# Release 编译
./build.sh release
```

编译产物：
- 可执行文件：`modules/build/bin/`
- 动态库：`modules/build/lib/`
- `compile_commands.json` 自动同步到项目根目录，供 clangd 使用

### 清理

```bash
./build.sh clean
```

### 单模块独立编译

每个子模块均可独立编译，以 `protocol` 为例：

```bash
cd protocol
cmake -B build
cmake --build build
# 产物：build/liblight_protocol.a
```

## 运行测试

确保 Redis 服务已启动：

```bash
# 启动 Redis（如未运行）
redis-server --daemonize yes
```

在两个终端中分别运行：

**终端 1 — 启动 receiver（订阅者）：**

```bash
./modules/build/bin/receiver
```

**终端 2 — 运行 publisher（发布者）：**

```bash
./modules/build/bin/publisher
```

publisher 会发送一条消息（SET），等待 3 秒后删除（DEL），receiver 将打印收到的消息并在收到 DEL 事件后退出。

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

3. 重新执行 `./build.sh` 即可

## 添加新 Proto

1. 在 `protocol/proto/<type>/` 下创建 `xxx.proto`（proto3 语法）
2. 重新编译，CMake 会自动发现新文件并生成对应的 `.pb-c.c/.h`
3. C 代码中引用：

```c
#include "<type>/xxx.pb-c.h"

MyMessage msg = MY_MESSAGE__INIT;
```

无需手动修改 CMakeLists.txt。

## 技术栈

| 组件 | 选型 | 用途 |
|------|------|------|
| 消息总线 | Redis | KV 存储 + Pub/Sub 事件通知 |
| 数据格式 | protobuf-c | 跨语言序列化，强类型校验 |
| Redis 客户端 | hiredis | C 语言 Redis 异步/同步驱动 |
| 构建系统 | CMake 3.10+ | 全量编译 / 单模块独立构建 |
| 编译器 | GCC | C 语言编译 |
