#!/bin/bash
#
# check_deps.sh — 校验 lite_switch 依赖是否就绪
#
# 用法:
#   ./scripts/check_deps.sh
#
set -euo pipefail

PASS=0
FAIL=0
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

check_pkg() {
    local name="$1"
    if pkg-config --exists "$name" 2>/dev/null; then
        local ver
        ver=$(pkg-config --modversion "$name" 2>/dev/null)
        echo "  [OK] $name ($ver)"
        PASS=$((PASS + 1))
    else
        echo "  [MISSING] $name"
        FAIL=$((FAIL + 1))
    fi
}

check_cmd() {
    local name="$1"
    if command -v "$name" &>/dev/null; then
        echo "  [OK] $name ($(command -v "$name"))"
        PASS=$((PASS + 1))
    else
        echo "  [MISSING] $name"
        FAIL=$((FAIL + 1))
    fi
}

check_redis() {
    if systemctl is-active --quiet redis-server 2>/dev/null; then
        echo "  [OK] redis-server (running via systemd)"
        PASS=$((PASS + 1))
    elif pgrep -x redis-server &>/dev/null; then
        echo "  [OK] redis-server (running)"
        PASS=$((PASS + 1))
    else
        echo "  [WARN] redis-server (not running)"
        echo "         手动启动: redis-server --daemonize yes"
        echo "         或: systemctl start redis-server"
    fi
}

echo "==> 校验编译工具链..."
check_cmd cmake
check_cmd gcc
check_cmd pkg-config
check_cmd python3

echo ""
echo "==> 校验系统库..."

# hiredis / zlog — 来自 git submodule，检查是否已初始化
if [ -f "${ROOT}/deps/hiredis/hiredis.h" ] && [ -f "${ROOT}/deps/zlog/src/zlog.h" ]; then
    echo "  [OK] hiredis + zlog (git submodule)"
    PASS=$((PASS + 1))
else
    echo "  [MISSING] hiredis / zlog (git submodule 未初始化)"
    echo "         请执行: git submodule update --init --recursive"
    FAIL=$((FAIL + 1))
fi

check_pkg libprotobuf-c
check_pkg readline

echo ""
echo "==> 校验 Proto 编译工具..."
check_cmd protoc
check_cmd protoc-gen-c
check_cmd protoc-c

echo ""
echo "==> 校验 Redis..."
check_redis


echo ""
echo "==> 校验 Intel FM10840 SDK 头文件..."
SDK_TAR="${ROOT}/scripts/sdk_headers.tar.gz"
SDK_DIR="${ROOT}/modules/3.PD/4.SDA/sdk"
if [ -d "${SDK_DIR}/include" ] && [ -f "${SDK_DIR}/include/fm_sdk.h" ]; then
    echo "  [OK] SDK headers (extracted: ${SDK_DIR})"
    PASS=$((PASS + 1))
elif [ -f "${SDK_TAR}" ]; then
    echo "  [PENDING] SDK 压缩包存在但未解压"
    echo "           请执行: ./scripts/install_deps.sh"
    FAIL=$((FAIL + 1))
else
    echo "  [MISSING] SDK headers (${SDK_DIR})"
    echo "  [MISSING] SDK 压缩包 (${SDK_TAR})"
    echo "           请从开发机拷贝 sdk_headers.tar.gz 到 ${ROOT}/scripts/"
    echo "           拷贝后执行: ./scripts/install_deps.sh"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "==> 结果: ${PASS} 通过, ${FAIL} 缺失"
if [ "$FAIL" -gt 0 ]; then
    echo "    请运行: ./scripts/install_deps.sh（以 root 执行）"
    exit 1
fi
