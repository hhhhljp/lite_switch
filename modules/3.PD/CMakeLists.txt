cmake_minimum_required(VERSION 3.10)
project(lite_switch_pd C)

set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
while(NOT EXISTS "${_d}/cmake/aggregate.cmake")
  get_filename_component(_d "${_d}" DIRECTORY)
endwhile()
include("${_d}/cmake/aggregate.cmake")
unset(_d)
