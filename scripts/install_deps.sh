#!/bin/bash
#
# install_deps.sh — 一键安装 lite_switch 所有依赖
#
# 用法:
#   sudo ./scripts/install_deps.sh
#
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "请使用 sudo 运行: sudo ./scripts/install_deps.sh"
    exit 1
fi

echo "==> 安装 lite_switch 编译依赖..."

apt update

apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libhiredis-dev \
    libreadline-dev \
    protobuf-compiler \
    protobuf-c-compiler \
    libprotobuf-c-dev \
    redis-server

echo ""
echo "==> 启动 Redis..."

# 检测 Redis 是否已在运行
if pgrep -x redis-server &>/dev/null; then
    echo "    Redis 已在运行，跳过启动。"
else
    # 检测 systemd 是否可用（容器环境通常不可用）
    if command -v systemctl &>/dev/null && systemctl is-system-running --quiet 2>/dev/null; then
        systemctl enable redis-server 2>/dev/null || true
        systemctl start redis-server 2>/dev/null || true
    fi

    # 如果 systemd 方式未成功，直接启动
    if ! pgrep -x redis-server &>/dev/null; then
        echo "    检测到容器环境，使用 redis-server --daemonize yes 直接启动..."
        redis-server --daemonize yes 2>/dev/null || true
    fi

    # 最终确认
    if pgrep -x redis-server &>/dev/null; then
        echo "    Redis 启动成功。"
    else
        echo "    [WARN] Redis 启动失败，请手动启动后重试。"
    fi
fi

echo ""
echo "==> 安装完成。"
