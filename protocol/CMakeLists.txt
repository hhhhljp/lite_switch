cmake_minimum_required(VERSION 3.10)
project(lite_switch_protocol C)

# ── 查找 protobuf-c 工具链 ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

find_program(PROTOC protoc REQUIRED)
find_program(PROTOC_GEN_C protoc-gen-c REQUIRED)

# ── 目录配置 ──
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ── 收集所有 .proto 源文件 ──
file(GLOB_RECURSE PROTO_FILES "${PROTO_DIR}/*.proto")

# ── 遍历编译每个 .proto ──
foreach(PROTO_FILE ${PROTO_FILES})
  # 计算相对路径，产出目录镜像 proto/ 结构
  file(RELATIVE_PATH REL_PATH "${PROTO_DIR}" "${PROTO_FILE}")
  get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY)
  get_filename_component(FILE_STEM "${PROTO_FILE}" NAME_WE)

  set(OUT_DIR "${INCLUDE_DIR}/${REL_DIR}")
  set(PB_C_FILE "${OUT_DIR}/${FILE_STEM}.pb-c.c")
  set(PB_H_FILE "${OUT_DIR}/${FILE_STEM}.pb-c.h")

  add_custom_command(
    OUTPUT  ${PB_C_FILE} ${PB_H_FILE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}
    COMMAND ${PROTOC}
      --plugin=protoc-gen-c=${PROTOC_GEN_C}
      --c_out=${INCLUDE_DIR}
      --proto_path=${PROTO_DIR}
      ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Proto → ${REL_DIR}/${FILE_STEM}.pb-c.[ch]"
  )

  list(APPEND GEN_SRCS ${PB_C_FILE})
  list(APPEND GEN_HDRS ${PB_H_FILE})
endforeach()

# ── 将生成产物聚合为静态库 ──
add_custom_target(proto_generate DEPENDS ${GEN_SRCS} ${GEN_HDRS})

add_library(light_protocol STATIC ${GEN_SRCS})
add_dependencies(light_protocol proto_generate)
target_include_directories(light_protocol PUBLIC ${INCLUDE_DIR})
target_link_libraries(light_protocol PUBLIC ${PROTOBUF_C_LIBRARIES})

# ── CLI（proto-aware Redis 客户端，依赖 proto 编译产物） ──
add_subdirectory(cli)
