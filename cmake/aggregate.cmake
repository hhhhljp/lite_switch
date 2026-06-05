# aggregate.cmake — 通用模块聚合器
#
# 使用方式（任意层级）：
#   cmake_minimum_required(VERSION 3.10)
#   project(模块名 C)
#   include(<相对于工程根目录>/cmake/aggregate.cmake)
#
# 自动遍历当前目录下所有包含 CMakeLists.txt 的子目录并编译。
# 依赖由各子组件自行管理（通过 if(NOT TARGET ...) 守卫避免重复）。

file(GLOB _agg_subdirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(_agg_dir ${_agg_subdirs})
  if(IS_DIRECTORY "${_agg_dir}" AND EXISTS "${_agg_dir}/CMakeLists.txt")
    get_filename_component(_agg_name "${_agg_dir}" NAME)
    add_subdirectory(${_agg_name})
  endif()
endforeach()

unset(_agg_subdirs)
unset(_agg_dir)
unset(_agg_name)
