# lite_switch 项目开发大纲

## 1. 项目概述

### 1.1 项目目标

`lite_switch` 是一个基于 Intel FM10840 Switch Chip 的完整交换机软件框架，采用分层松耦合架构，各层通过 Redis + Protobuf 进行进程间通信。

1. **分层解耦**：将交换机软件拆分为独立进程，每层职责清晰，可独立开发、调试、替换
2. **数据驱动**：以 Redis 为唯一消息总线，Protobuf 为统一数据格式，实现各层间松耦合通信
3. **硬件抽象**：`ies_sda` 作为唯一硬件相关模块，其余各层完全与芯片型号无关
4. **可组合**：`./build.sh` 一键编译，单模块编译支持快速调试迭代

### 1.2 技术栈

| 组件 | 选型 | 用途 |
|------|------|------|
| 消息总线 | Redis 8.0 | KV 存储 + Pub/Sub 事件通知 |
| 数据格式 | protobuf-c 1.5 | 跨语言序列化，强类型校验 |
| Redis 客户端 | hiredis 1.2 | C 语言 Redis 异步/同步驱动 |
| 构建系统 | CMake 3.10+ | 全量编译 / 单模块独立构建 |
| 编译工具链 | GCC 15.2 | C 语言编译 |

### 1.3 架构设计理念（SONiC 对齐）

```
┌──────────────────────────────────────────────────┐
│  SONiC 层                   lite_switch 层       │
├──────────────────────────────────────────────────┤
│  Management Plane    →    1.UI (CLI / Web)       │
│  CONFIG_DB           →    CONFIG_DB (Redis DB#0)  │
│  Control Plane       →    2.PI (protocol control) │
│  APPL_DB             →    STATE_DB (Redis DB#1)   │
│  Orchestration Layer →    3.PD (hardware abstract)│
│  ASIC_DB             →    FWD_DB (Redis DB#2)     │
│  Data Plane          →    4.SDA (SDK pass-through)│
└──────────────────────────────────────────────────┘
```

---

## 2. 工程目录结构

```
lite_switch/
├── PROJECT_OUTLINE.md          # 项目大纲（本文件）
├── build.sh                    # 编译 / 清理 控制脚本
├── cmake/                      # CMake 辅助模块
│   ├── comp_base.cmake         # 单组件编译宏（自动链接 middleware + protocol）
│   └── aggregate.cmake         # 模块聚合器（自动发现子目录并编译）
├── middleware/                  # 中间件层
│   ├── CMakeLists.txt          # 编译为 libmiddleware.so
│   ├── include/
│   │   └── middleware.h         # 公开 API 头文件
│   └── src/
│       └── middleware.c         # Redis 连接管理、KV 操作、Pub/Sub 实现
├── protocol/                   # Protobuf 定义与编译
│   ├── CMakeLists.txt          # proto → .pb-c.c/.h，编译为 liblight_protocol.a
│   ├── README.md               # 模块使用说明
│   ├── proto/                  # .proto 源文件
│   │   └── test/
│   │       └── test.proto      # 测试用消息定义（LightTestKey / LightTestEntry）
│   └── include/                # 编译产物（生成后存在；clean 时清除）
│       └── test/
│           ├── test.pb-c.h
│           └── test.pb-c.c
├── modules/                    # 各层模块
│   ├── CMakeLists.txt          # 顶层构建入口（aggregate.cmake 聚合子目录）
│   ├── 1.UI/                   # 用户接口层
│   │   ├── 1.CLI/              # CLI 模块（待实现）
│   │   └── 2.Web/              # Web 管理界面（待实现）
│   ├── 2.PI/                   # 协议控制层（待实现）
│   ├── 3.PD/                   # 数据处理层
│   │   ├── CMakeLists.txt      # aggregate.cmake 自动遍历子目录
│   │   ├── 1.Bridge/           # 桥接模块（待实现）
│   │   ├── 2.Route/            # 路由模块（待实现）
│   │   ├── 3.test/             # 测试模块（Pub/Sub 回调模型验证）
│   │   │   ├── CMakeLists.txt  # aggregate 入口
│   │   │   ├── publisher/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── main.c      # SET → sleep → DEL
│   │   │   └── receiver/
│   │   │       ├── CMakeLists.txt
│   │   │       └── main.c      # 订阅 keyspace → 回调打印 → del 时退出
│   │   └── 4.SDA/              # SDK 透传层（待实现）
│   │       └── (ies_sda 适配层)
└── scripts/                    # 构建和运维脚本（待扩展）
```

---

## 3. build.sh — 工程编译控制脚本

### 3.1 用法

```bash
./build.sh          # Debug 编译（默认）
./build.sh build    # 同上
./build.sh release  # Release 编译
./build.sh clean    # 清除所有编译产物
```

### 3.2 构建流程

```
./build.sh
  ├─ cmake -S modules/ -B modules/build          # 以 modules/ 为入口
  │   └─ modules/CMakeLists.txt
  │       └─ aggregate.cmake 遍历子目录
  │           └─ 3.PD/CMakeLists.txt
  │               └─ aggregate.cmake 遍历 3.test/publisher, 3.test/receiver
  │                   └─ comp_base.cmake
  │                       ├─ add_subdirectory(middleware)
  │                       ├─ add_subdirectory(protocol)   # proto 自动生成
  │                       └─ add_light_component(name ...)
  └─ cmake --build modules/build -- -j$(nproc)
```

**产物位置：**

| 类型 | 路径 |
|------|------|
| 可执行文件 | `modules/build/bin/publisher`, `receiver` |
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
```

---

## 4. middleware — 中间件层

### 4.1 设计目标

提供 Redis 操作的统一封装，组件代码不直接依赖 hiredis / protobuf-c 底层 API。
所有 namespace 前缀、订阅模式构建、value 反序列化由 middleware 内部完成。

### 4.2 API 概览

#### 连接管理

```c
mw_context_t *mw_connect("127.0.0.1", 6379);
void          mw_disconnect(ctx);
int           mw_select_db(ctx, db);
```

#### KV 操作（裸二进制）

```c
int mw_set(ctx, key, data, len);
int mw_get(ctx, key, &data, &len);
int mw_del(ctx, key);
```

#### Protobuf Message 操作（带 namespace 前缀自动派生）

```c
int mw_set_message(ctx, &key_msg, &val_msg);
int mw_del_message(ctx, &key_msg);
ProtobufCMessage *mw_get_message(ctx, &key_msg, &val_desc);
```

Redis key 格式：`s_{package}/ + packed(key_msg)`

`package` 由 `key_msg->descriptor->package_name` 自动提取（来自 proto 中 `package xxx;` 声明）。

#### Hash 操作

```c
int mw_hset(ctx, key, field, data, len);
int mw_hget(ctx, key, field, &data, &len);
int mw_hgetall(ctx, key, &pairs, &count);
int mw_hmupdate(ctx, key, pairs, count);
```

#### Pub/Sub 回调注册

```c
typedef void (*mw_notify_cb)(const char *event, void *value, void *user_data);

typedef struct {
    const ProtobufCMessage *key_msg;   // proto key 实例指针
    const ProtobufCMessage *entry_msg; // proto entry 模板（仅取 descriptor）
    mw_notify_cb            cb;        // 回调函数
} mw_callback_entry;

int mw_subscribe_keys(ctx, entries, count);
int mw_poll(ctx, timeout_ms);  // 首次调用自动完成 PSUBSCRIBE
```

#### 资源释放

```c
void mw_free_data(data);
void mw_free_kv(pairs, count);
void mw_free_message(msg);  // 释放回调中收到的 value
```

### 4.3 Pub/Sub 数据流

```
组件注册回调
  │
  ├─ mw_subscribe_keys(ctx, callbacks, N)
  │   ├─ 存储 key_prefix = "s_{package}/"
  │   ├─ 存储 packed(key_msg) 二进制
  │   └─ 存储 val_desc = entry_msg->descriptor
  │
  ▼
mw_poll(ctx, -1)  首次调用时延迟完成:
  │
  ├─ 创建独立 Redis 连接
  ├─ 去重收集所有 key_prefix
  ├─ PSUBSCRIBE __keyspace@*__:<prefix>*
  │
  ▼
循环读取 Pub/Sub 消息:
  │
  ├─ 收到 pmessage → 提取 channel 中的 Redis key
  ├─ 前缀匹配: strncmp(rkey, key_prefix, plen) == 0
  ├─ 精确匹配: memcmp(rkey+plen, key_packed, key_len) == 0
  ├─ "set" 事件 → Redis GET → protobuf_c_message_unpack → 传入 value
  ├─ "del" 事件 → value = NULL
  └─ 调用 cb(event, value, user_data)
```

### 4.4 回调架构原则

- **组件只声明 key + callback 的关联**，不手写任何 namespace 前缀、不关心反序列化
- 订阅管理、模式构建、事件分发全部由 middleware 内部完成
- `mw_callback_entry` 只需传 proto 变量指针，descriptor 自动提取

---

## 5. protocol — Protobuf 定义与编译

### 5.1 模块结构

```
protocol/
├── CMakeLists.txt       # 自动扫描 proto/ 下所有 .proto 并编译
├── proto/               # .proto 源文件（按资源类型分子目录）
│   └── test/
│       └── test.proto
└── include/             # 编译产物（自动生成，clean 时清除）
    └── test/
        ├── test.pb-c.c
        └── test.pb-c.h
```

### 5.2 Proto 消息定义（test.proto）

```protobuf
syntax = "proto3";
package test;                          // 自动生成 namespace 前缀 "s_test/"

message Light_TestKey {
  string test_name = 1;
  uint32 test_id   = 2;
}

message Light_TestEntry {
  Light_TestKey index     = 1;         // 内嵌 key
  string        test_name = 2;
  string        data      = 3;         // payload
  uint32        data_len  = 4;
}
```

### 5.3 添加新 Proto 的步骤

1. 在 `proto/<type>/` 下创建 `xxx.proto`（声明 `package <pkg>;`）
2. 消息中内嵌 key message（参考 `Light_TestEntry.index`）
3. 重新 `./build.sh` 即可，无需修改 CMakeLists.txt

生成的 C 标识符由 protobuf-c 自动命名：
- 消息类型: `<Pkg>__<MsgName>`（如 `Test__LightTestEntry`）
- 初始化宏: `<PKG>__<MSG>__INIT`（如 `TEST__LIGHT__TEST_ENTRY__INIT`）
- 描述符: `&<pkg>__<msg>__descriptor`（如 `&test__light__test_entry__descriptor`）

---

## 6. cmake 构建辅助模块

### 6.1 comp_base.cmake

单组件编译宏，自动引入 middleware 和 protocol 依赖：

```cmake
add_light_component(receiver main.c)
# 等价于:
#   add_executable(receiver main.c)
#   target_link_libraries(receiver middleware light_protocol)
#   产物 → build/bin/
```

### 6.2 aggregate.cmake

通用模块聚合器：自动遍历当前目录下所有含 CMakeLists.txt 的子目录并编译。
用于 `modules/CMakeLists.txt` 和 `modules/3.PD/CMakeLists.txt` 等层级。

---

## 7. 已实现的测试组件

### 7.1 publisher — 数据发布者

```
publisher/
├── CMakeLists.txt  → add_light_component(publisher main.c)
└── main.c
```

**流程：**
1. 构造 `Test__LightTestKey` + `Test__LightTestEntry` 数据
2. `mw_connect()` 连接 Redis
3. `mw_set_message(ctx, &key_body, &entry)` — 写入 Redis（key 自动生成为 `s_test/...`）
4. `sleep(3)` 等待 receiver 响应
5. `mw_del_message(ctx, &key_body)` — 删除 key
6. `mw_disconnect(ctx)`

**组件不关心：** namespace 前缀、Redis key 格式、序列化细节

### 7.2 receiver — 事件订阅者

```
receiver/
├── CMakeLists.txt  → add_light_component(receiver main.c)
└── main.c
```

**流程：**
1. 声明 `Test__LightTestKey` + `Test__LightTestEntry` 静态变量
2. 定义 `on_light_event(event, value, arg)` 回调
   - `"set"` → 打印 `entry->index->test_name`, `entry->index->test_id`, `entry->data`
   - `"del"` → 打印并 `exit(0)`
3. `mw_connect()` → `mw_subscribe_keys(ctx, callbacks, N)` → `mw_poll(ctx, -1)` 阻塞等待

**组件不关心：** PSUBSCRIBE 模式、Redis key 提取、反序列化、descriptor 管理

### 7.3 测试结果验证

```
Terminal 1: ./modules/build/bin/receiver
  Connected to Redis
  Waiting for events...
  [SET] test_name=lightkey  test_id=100  |  data=hello world  data_len=12
  [DEL] Received del event, exiting...

Terminal 2: ./modules/build/bin/publisher
  SET ...
  DEL ...
```

---

## 8. Redis Key 命名规范

| 格式 | 示例 | 说明 |
|------|------|------|
| `s_{package}/ + packed(key)` | `s_test/\x0a\x08lightkey\x10\x64` | middleware 自动生成 |
| keyspace channel | `__keyspace@0__:s_test/*` | middleware 自动订阅 |

key 命名由 proto 的 `package` 声明驱动，无需组件手写。

---

## 9. 待实现的模块

### 9.1 按优先级

| 优先级 | 模块 | 路径 | 职责 |
|--------|------|------|------|
| P0 | ies_sda 适配 | `modules/3.PD/4.SDA/` | Intel FM10840 SDK 原子操作封装 |
| P1 | fwd | `modules/3.PD/1.Bridge/` | 硬件资源抽象，状态缓存，操作编排 |
| P2 | ctrl | `modules/2.PI/` | VLAN/LAG/STP 配置逻辑 |
| P3 | cli | `modules/1.UI/1.CLI/` | 文本命令 → Protobuf，结果展示 |

---

## 10. 开发约定

### 10.1 Proto 规范

- 每个业务资源定义一对 key + entry message
- key message 内嵌所有定位字段
- entry message 的 `index` 字段指向 key message
- 必须声明 `package xxx;`

### 10.2 组件编码规范

- 组件只通过 `middleware.h` 访问 Redis，不直接使用 hiredis/protobuf-c
- 回调签名统一：`void cb(const char *event, void *value, void *arg)`
- value 使用后必须调用 `mw_free_message()` 释放
- CMakeLists.txt 使用 `add_light_component(name src.c ...)` 宏

### 10.3 构建约定

- 全量编译：`./build.sh`
- 清理产物：`./build.sh clean`
- 不向版本库提交 `modules/build/`, `protocol/include/`, `.cache/`, `compile_commands.json`
