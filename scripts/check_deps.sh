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
        echo "  [OK] redis-server (running)"
        PASS=$((PASS + 1))
    elif pgrep -x redis-server &>/dev/null; then
        echo "  [OK] redis-server (running)"
        PASS=$((PASS + 1))
    else
        echo "  [WARN] redis-server (not running, 请执行 systemctl start redis)"
    fi
}

echo "==> 校验编译工具链..."
check_cmd cmake
check_cmd gcc

echo ""
echo "==> 校验系统库..."
check_pkg hiredis
check_pkg libprotobuf-c

echo ""
echo "==> 校验 Proto 编译工具..."
check_cmd protoc
check_cmd protoc-gen-c

echo ""
echo "==> 校验 Redis..."
check_redis

echo ""
echo "==> 结果: ${PASS} 通过, ${FAIL} 缺失"
if [ "$FAIL" -gt 0 ]; then
    echo "    请运行: sudo ./scripts/install_deps.sh"
    exit 1
fi
