# comp_base.cmake — 单个组件编译的通用宏
#
# 使用方式（任意层级）：
#   cmake_minimum_required(VERSION 3.10)
#   project(组件名 C)
#
#   set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
#   while(NOT EXISTS "${_d}/cmake/comp_base.cmake")
#     get_filename_component(_d "${_d}" DIRECTORY)
#   endwhile()
#   include("${_d}/cmake/comp_base.cmake")
#   unset(_d)
#
#   add_light_component(组件名 main.c other.c)

# ── 工程根目录（本文件位于 <ROOT>/cmake/ 下） ──
get_filename_component(LIGHT_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# ── 引入依赖工程（若尚未引入，防重复） ──
if(NOT TARGET middleware)
  add_subdirectory(${LIGHT_ROOT}/middleware ${CMAKE_BINARY_DIR}/middleware)
endif()
if(NOT TARGET light_protocol)
  add_subdirectory(${LIGHT_ROOT}/protocol   ${CMAKE_BINARY_DIR}/protocol)
endif()

# ── 通用宏：添加一个组件可执行文件 ──
#   add_light_component(name source1.c source2.c ...)
#   自动链接 middleware + light_protocol，产物输出到 build/bin/
macro(add_light_component NAME)
  add_executable(${NAME} ${ARGN})
  target_include_directories(${NAME} PRIVATE
    ${LIGHT_ROOT}/middleware/include
    ${LIGHT_ROOT}/protocol/include
  )
  target_link_libraries(${NAME}
    middleware
    light_protocol
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endmacro()

# ── compile_commands.json 自动同步到工程根目录 ──
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
  "Generate compile_commands.json for clangd" FORCE)

if(NOT TARGET sync_compile_commands)
  add_custom_target(sync_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      "${LIGHT_ROOT}/compile_commands.json"
    COMMENT "compile_commands.json → ${LIGHT_ROOT}/"
    VERBATIM
  )
endif()
