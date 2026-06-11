#!/bin/bash
# Build redis-cli-proto using Redis 8.0.5 source + protocol/include/ proto objects.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROTOCOL_DIR="${SCRIPT_DIR}/.."
INCLUDE_DIR="${PROTOCOL_DIR}/include"
REDIS_SRC="/tmp/redis-src"
REDIS_VER="8.0.5"

# ── Step 1: Generate proto_registry.c ──
echo "==> Generating proto registry..."
bash "${PROTOCOL_DIR}/gen_proto_registry.sh"

# ── Step 2: Clone Redis if needed ──
if [ ! -d "$REDIS_SRC/src" ]; then
    echo "==> Cloning Redis ${REDIS_VER}..."
    git clone --depth 1 --branch "$REDIS_VER" \
        https://github.com/redis/redis.git "$REDIS_SRC" 2>&1 | tail -1
fi

cd "$REDIS_SRC"
git checkout -- src/redis-cli.c src/Makefile 2>/dev/null || true

# ── Step 3: Copy sources ──
cp "${SCRIPT_DIR}/cli_proto.c"      "$REDIS_SRC/src/"
cp "${SCRIPT_DIR}/cli_proto.h"      "$REDIS_SRC/src/"
cp "${SCRIPT_DIR}/proto_registry.c" "$REDIS_SRC/src/"

# ── Step 4: Patch redis-cli.c with Python ──
echo "==> Patching redis-cli.c..."
python3 "${SCRIPT_DIR}/patch_redis_cli.py" src/redis-cli.c

# ── Step 5: Patch Makefile ──
echo "==> Patching Makefile..."
sed -i 's/\(REDIS_CLI_OBJ=.*cli_commands.o\)/\1 cli_proto.o/' src/Makefile
sed -i "/FINAL_CFLAGS+=.*hdr_histogram.*fast_float\$/a FINAL_CFLAGS+= \$(shell pkg-config --cflags libprotobuf-c 2>/dev/null) -I${PROTOCOL_DIR} -I${SCRIPT_DIR}/../../deps" src/Makefile

# Add -lprotobuf-c to link line (before PROTO_OBJS placeholder)
sed -i '/^\$(REDIS_CLI_NAME): \$(REDIS_CLI_OBJ)/,/^$/{ s|$(TLS_CLIENT_LIBS)|& -lprotobuf-c PROTO_OBJS_PLACEHOLDER|; }' src/Makefile

# ── Step 6: Compile proto .pb-c.c → .o ──
echo "==> Compiling proto objects..."
mkdir -p "${REDIS_SRC}/src/proto_objs"
shopt -s globstar 2>/dev/null || true
PROTO_OBJS=""
for f in "$INCLUDE_DIR"/**/*.pb-c.c; do
    [ -f "$f" ] || continue
    name="$(basename "$f" .c)"
    out="${REDIS_SRC}/src/proto_objs/${name}.o"
    cc -c -std=c99 -O2 -D_DEFAULT_SOURCE \
        -I"$INCLUDE_DIR" \
        $(pkg-config --cflags libprotobuf-c 2>/dev/null) \
        "$f" -o "$out"
    PROTO_OBJS="${PROTO_OBJS} ${out}"
done
sed -i "s|PROTO_OBJS_PLACEHOLDER|${PROTO_OBJS}|" src/Makefile
echo "   proto objects:${PROTO_OBJS}"

# ── Step 7: Build ──
echo "==> Building redis-cli..."
make -C src redis-cli MALLOC=libc 2>&1 | tail -5

# ── Step 8: Install ──
BIN_DIR="${PROTOCOL_DIR}/../modules/build/bin"
mkdir -p "$BIN_DIR"
cp src/redis-cli "$BIN_DIR/redis-cli-proto"
echo ""
echo "Done: ${BIN_DIR}/redis-cli-proto"
"${BIN_DIR}/redis-cli-proto" --version 2>/dev/null || true
