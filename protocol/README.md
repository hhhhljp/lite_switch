# protocol — Proto 定义、编译与 CLI 诊断工具

## 目录结构

```
protocol/
├── CMakeLists.txt                 # proto 编译 (protoc-c → include/)
├── gen_proto_registry.sh          # 扫描 include/*.pb-c.h → 生成 cli/proto_registry.c
├── proto/                         # .proto 源文件（唯一来源）
│   ├── PD/interface/interface.proto
│   └── test/test.proto
├── include/                       # 编译产物（gitignored）
│   ├── PD/interface/interface.pb-c.{c,h}
│   └── test/test.pb-c.{c,h}
└── cli/
    ├── CMakeLists.txt              # add_custom_target(redis_cli_proto)
    ├── build_redis_cli.sh          # 克隆 Redis 8.0.5 → patch → 构建
    ├── patch_redis_cli.py          # Python patcher（5 处修改）
    ├── cli_proto.{c,h}             # protobuf 反射解析引擎（零硬编码）
    └── proto_registry.c            # 自动生成（gitignored）
```

产物：
- 静态库: `build/liblight_protocol.a`
- CLI 诊断工具: `../modules/build/bin/redis-cli-proto`

## 添加新 Proto

1. 在 `proto/<type>/` 下创建 `xxx.proto`（使用 proto3 语法）
2. 重新编译即可，CMake 自动发现新文件并生成 `.pb-c.c/.h`
3. `gen_proto_registry.sh` 自动扫描新 descriptor，无需手动修改注册表

## redis-cli-proto 功能

基于 Redis 8.0.5 原生 redis-cli + `--proto` protobuf 解析。字段反射实现，零硬编码。

### 用法

```bash
redis-cli-proto --proto            # 启动 REPL（proto 模式）
redis-cli-proto --proto KEYS "*s_*"               # 二进制 key → 可读路径
redis-cli-proto --proto GET "s_pd_interface/sw=0, port=1"  # proto  payload 完整解析
redis-cli-proto                     # 不加 --proto = 原生 redis-cli
```

### `--proto` 模式行为

| 命令 | 输入翻译 | 输出翻译 |
|------|---------|---------|
| KEYS / SCAN | pattern 中人类可读路径 → 二进制模式 | 二进制 key → `"prefix/sw=0, port=1"` |
| GET | key 人类可读 → 二进制 | protobuf 反射多行打印 |
| SET | key 人类可读 → 二进制，value 人类可读 → 二进制 | 原生 "OK" |
| DEL / EXISTS / TYPE / TTL | key 人类可读 → 二进制 | 原生行为 |
| MONITOR | — | 隐藏原始行，紧凑单行 proto 解析 |

### MONITOR 输出示例

```
1780936015.123456 [0 127.0.0.1:12345] "SET" "s_pd_interface/sw=0,port=0" "port_name=25G-Port,speed=SPEED_25G,admin_mode=ADMIN_DOWN,..."
```
MONITOR 输出格式与 `SET` 输入格式完全一致，可直接复制粘贴用于调试。

### 设计要点

- **entry->index 无需手动填充**：中间件 `mw_poll_one` 会自动从 Redis key 解析并覆盖。组件的 `mw_set_message` 和 `redis-cli-proto SET` 均不需要在 value 中写入 index。
- **GET 输出**：无冗余 key 行，跳过 `index` 子消息，字符串字段不带引号包裹
- **KEYS 紧凑输出**：逗号分隔无空格，如 `"s_pd_interface/sw=0,port=0"`

### 已知限制

1. **proto key 必须包含完整前缀**。如 `s_pd_interface/sw=0,port=1` 而非 `port=1`
2. **proto3 零值字段在二进制编码中省略**（`has_` flag 仅在非零时置位），因此 `sw=0` 在 KEYS 过滤时匹配所有 key。非零字段（如 `sw=99`）过滤完全正常
3. **proto key 含逗号+空格**（如 `sw=0, port=1`）时，REPL 中必须加引号，否则被按空格拆词
