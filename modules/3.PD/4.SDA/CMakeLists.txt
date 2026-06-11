cmake_minimum_required(VERSION 3.10)
project(sda C)

# ── SDA_NO_HW: 无硬件编译模式（默认 ON） ──
option(SDA_NO_HW "Build without hardware/SDK dependencies" ON)

# ── 定位 lite_switch 根目录并引入 comp_base.cmake ──
set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
while(NOT EXISTS "${_d}/cmake/comp_base.cmake")
  get_filename_component(_d "${_d}" DIRECTORY)
  if("${_d}" STREQUAL "/")
    message(FATAL_ERROR "Cannot find lite_switch root (comp_base.cmake)")
  endif()
endwhile()
include("${_d}/cmake/comp_base.cmake")
unset(_d)

# ── 源文件列表 ──
set(SDA_SRCS
    main.c
    init/sdk_init.c
    intf/intf_api.c
    intf/intf_cb.c
    intf/intf_event.c
    event/sda_event_sim.c
    event/sda_event.c
)

# ── 使用 add_light_component 宏创建可执行文件 ──
#     自动链接 middleware + light_protocol，产物 → build/bin/
add_light_component(sda ${SDA_SRCS})

# ── 追加 SDA 私有头文件路径 ──
target_include_directories(sda PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/event
)

# ── 编译选项 ──
target_compile_options(sda PRIVATE -std=c99 -Wall -Wextra)

# POSIX 扩展（sigaction 等），与硬件模式无关
target_compile_definitions(sda PRIVATE
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)

if(SDA_NO_HW)
    target_compile_definitions(sda PRIVATE SDA_NO_HW)
else()
    # 硬件模式：需要 SDK 头文件和库
    target_include_directories(sda PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/alos
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/alos/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/platforms/libertyTrail
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/support
    )
    target_link_directories(sda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/lib)
    target_link_libraries(sda
        FocalpointSDK
        LTStdPlatform
        support
        pthread
        rt
        dl
        m
    )
endif()