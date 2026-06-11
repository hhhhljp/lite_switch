#!/bin/bash
#
# build.sh — 工程编译 / 清理控制脚本
#
# 用法:
#   ./build.sh             # 仿真环境 Debug 编译（默认）
#   ./build.sh build       # 同上
#   ./build.sh debug       # 同上
#   ./build.sh hw          # 真实硬件环境 Debug 编译
#   ./build.sh release     # 仿真环境 Release 编译
#   ./build.sh release-hw  # 真实硬件环境 Release 编译
#   ./build.sh clean       # 清理所有编译产物和生成文件
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT}/modules/build"

# ──────────────────────────────────────────────
# 清理
# ──────────────────────────────────────────────
do_clean() {
    echo "==> 清理编译产物..."

    # 各子模块的 build 目录
    rm -rf "${ROOT}/modules/build"
    rm -rf "${ROOT}/protocol/build"
    rm -rf "${ROOT}/middleware/build"

    # CMake 缓存
    rm -rf "${ROOT}/modules/.cache"
    rm -rf "${ROOT}/.cache"

    # compile_commands.json
    rm -f "${ROOT}/compile_commands.json"

    # protobuf-c 生成产物 (protocol/include/)
    rm -rf "${ROOT}/protocol/include"

    # redis-cli-proto 中间产物
    rm -rf /tmp/redis-src
    rm -f "${ROOT}/protocol/cli/proto_registry.c"

    echo "    清理完成。"
}

# ──────────────────────────────────────────────
# 编译
# ──────────────────────────────────────────────
do_build() {
    # 初始化 git submodule（首次克隆后需要）
    git submodule update --init --recursive 2>/dev/null || true
    local build_type="$1"    # Debug | Release
    local sda_no_hw="$2"     # ON=仿真  OFF=真实硬件

    local mode_label
    if [ "$sda_no_hw" = "ON" ]; then
        mode_label="仿真"
    else
        mode_label="硬件"
    fi

    echo "==> CMake 配置 (${build_type}, ${mode_label}环境)..."
    cmake -S "${ROOT}/modules" \
          -B "${BUILD_DIR}" \
          -DCMAKE_BUILD_TYPE="${build_type}" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          -DSDA_NO_HW="${sda_no_hw}" \
          ${EXTRA_CMAKE_ARGS:-}

    echo ""
    echo "==> 开始编译..."
    cmake --build "${BUILD_DIR}" -- -j"$(nproc)"

    # 编译 redis-cli-proto（依赖 proto 生成产物）
    if [ -f "${ROOT}/protocol/include/test/test.pb-c.c" ]; then
        echo ""
        echo "==> 编译 redis-cli-proto..."
        cmake --build "${BUILD_DIR}" --target redis_cli_proto 2>&1 | tail -5
    fi

    echo ""
    echo "==> 编译完成。"
    echo "    产物目录: ${BUILD_DIR}/bin/"
    echo "    库目录:   ${BUILD_DIR}/lib/"
}

# ──────────────────────────────────────────────
# 入口
# ──────────────────────────────────────────────
case "${1:-build}" in
    build|debug|sim)
        do_build Debug ON
        ;;
    hw)
        do_build Debug OFF
        ;;
    release)
        do_build Release ON
        ;;
    release-hw)
        do_build Release OFF
        ;;
    clean)
        do_clean
        ;;
    *)
        echo "用法: $0 {build|debug|sim|hw|release|release-hw|clean}"
        exit 1
        ;;
esac
