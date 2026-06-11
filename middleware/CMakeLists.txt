cmake_minimum_required(VERSION 3.10)
project(lite_switch_middleware C)

# ── CMake 4.x 兼容旧版子项目的 cmake_minimum_required ──
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
# 静态库需要 -fPIC 以链接进 .so
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

# ── 编译依赖库（源码级，静态链接） ──
# hiredis: 静态库
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../deps/hiredis
                 ${CMAKE_BINARY_DIR}/deps/hiredis
                 EXCLUDE_FROM_ALL)

# zlog: 使用其静态库目标 zlog_s
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../deps/zlog
                 ${CMAKE_BINARY_DIR}/deps/zlog
                 EXCLUDE_FROM_ALL)

# ── protobuf-c（仍需系统安装，protoc 编译器无法轻易替换） ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

# ── 编译 middleware 动态库 ──
add_library(middleware SHARED
  src/middleware.c
)

target_include_directories(middleware PUBLIC
  include
  ${CMAKE_CURRENT_SOURCE_DIR}/../deps
  ${CMAKE_CURRENT_SOURCE_DIR}/../deps/zlog/src
)
target_link_libraries(middleware PUBLIC
  hiredis
  zlog_s
  ${PROTOBUF_C_LIBRARIES}
)
target_compile_options(middleware PRIVATE -Wall -fPIC)

set_target_properties(middleware PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
