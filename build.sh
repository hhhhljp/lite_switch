#!/bin/bash
#
# build.sh — 工程编译 / 清理控制脚本
#
# 用法:
#   ./build.sh             # 编译（默认 Debug）
#   ./build.sh build       # 同上
#   ./build.sh release     # Release 编译
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

    echo "    清理完成。"
}

# ──────────────────────────────────────────────
# 编译
# ──────────────────────────────────────────────
do_build() {
    local build_type="${1:-Debug}"

    echo "==> CMake 配置 (${build_type})..."
    cmake -S "${ROOT}/modules" \
          -B "${BUILD_DIR}" \
          -DCMAKE_BUILD_TYPE="${build_type}" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    echo ""
    echo "==> 开始编译..."
    cmake --build "${BUILD_DIR}" -- -j"$(nproc)"

    echo ""
    echo "==> 编译完成。"
    echo "    产物目录: ${BUILD_DIR}/bin/"
    echo "    库目录:   ${BUILD_DIR}/lib/"
}

# ──────────────────────────────────────────────
# 入口
# ──────────────────────────────────────────────
case "${1:-build}" in
    build|debug)
        do_build Debug
        ;;
    release)
        do_build Release
        ;;
    clean)
        do_clean
        ;;
    *)
        echo "用法: $0 {build|release|clean}"
        exit 1
        ;;
esac
