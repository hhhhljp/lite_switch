cmake_minimum_required(VERSION 3.10)
project(sda_event C)

set(EVENT_SRCS
    sda_event.c
    sda_event_sim.c
)

add_library(sda_event STATIC ${EVENT_SRCS})

target_include_directories(sda_event PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(sda_event PRIVATE -std=c99 -Wall -Wextra)
