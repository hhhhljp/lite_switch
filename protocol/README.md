# protocol — Proto 定义与编译

## 目录结构

```
protocol/
├── CMakeLists.txt           ← 本模块 CMake，可独立编译或被顶层调用
├── proto/                   ← 所有 .proto 源文件
│   └── <type>/              ← 按资源类型分子目录
│       └── xxx.proto
└── include/                 ← 编译产物（自动生成，不手动维护）
    └── <type>/              ← 与 proto/ 镜像结构
        ├── xxx.pb-c.c
        └── xxx.pb-c.h
```

## 添加新 Proto

1. 在 `proto/<type>/` 下创建 `xxx.proto`（使用 proto3 语法）
2. 重新编译即可，CMake 会自动发现新文件并生成对应的 `.pb-c.c/.h` 到 `include/<type>/`

无需手动修改 CMakeLists.txt — `file(GLOB_RECURSE)` 自动收集所有 `.proto`。

## 编译

**独立编译：**
```bash
cd protocol
cmake -B build
cmake --build build
```

**作为子模块被顶层调用：**
```cmake
# 顶层 CMakeLists.txt
add_subdirectory(protocol)
target_link_libraries(your_module light_protocol)
```

产物：
- 静态库: `build/liblight_protocol.a`
- 头文件: `include/<type>/xxx.pb-c.h`

## C 代码引用

```c
#include "<type>/xxx.pb-c.h"

// 初始化 message
TestEntry msg = TEST_ENTRY__INIT;
```
