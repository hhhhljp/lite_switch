cmake_minimum_required(VERSION 3.10)
project(switch_web C)

set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
while(NOT EXISTS "${_d}/cmake/comp_base.cmake")
  get_filename_component(_d "${_d}" DIRECTORY)
endwhile()
include("${_d}/cmake/comp_base.cmake")
unset(_d)

add_light_component(switch-web main.c)
