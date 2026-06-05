cmake_minimum_required(VERSION 3.10)
project(lite_switch_middleware C)

# ── 查找 hiredis 和 protobuf-c ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

# ── 编译为动态库 ──
add_library(middleware SHARED
  src/middleware.c
)

target_include_directories(middleware PUBLIC include)
target_link_libraries(middleware PUBLIC ${HIREDIS_LIBRARIES} ${PROTOBUF_C_LIBRARIES})
target_compile_options(middleware PRIVATE -Wall -fPIC)

# ── 安装到 build/lib ──
set_target_properties(middleware PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
