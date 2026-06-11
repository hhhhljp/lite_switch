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
    # ── 硬件模式：SDK 路径配置 ──
    # SDA_SDK_ROOT: SDK 安装根目录（ies/ 的父目录或直接 ies/）
    #   默认: ${CMAKE_CURRENT_SOURCE_DIR}/sdk（本地的头文件副本，gitignored）
    #   实机: cmake -DSDA_SDK_ROOT=/opt/intel/fm10k/ies ..
    if(NOT DEFINED SDA_SDK_ROOT)
        set(SDA_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
    endif()
    if(EXISTS "${SDA_SDK_ROOT}/include")
        set(_sdk_inc "${SDA_SDK_ROOT}/include")
    else()
        set(_sdk_inc "${SDA_SDK_ROOT}")
    endif()

    target_include_directories(sda PRIVATE
        ${_sdk_inc}
        ${_sdk_inc}/alos
        ${_sdk_inc}/alos/linux
        ${_sdk_inc}/platforms/libertyTrail
        ${_sdk_inc}/support
    )

    # 库路径：默认 ${SDA_SDK_ROOT}/lib，可单独覆盖
    if(NOT DEFINED SDA_SDK_LIB)
        set(SDA_SDK_LIB "${SDA_SDK_ROOT}/lib")
    endif()
    target_link_directories(sda PRIVATE ${SDA_SDK_LIB})
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