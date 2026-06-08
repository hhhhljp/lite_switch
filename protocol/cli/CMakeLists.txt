# This directory contains the proto-aware redis-cli integration.
# Build is handled by build_redis_cli.sh (called as a custom target),
# which clones Redis 8.0.5, applies the patch, and links against
# protocol/include/*.pb-c.c proto objects.
# Output: modules/build/bin/redis-cli-proto

add_custom_target(redis_cli_proto
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/build_redis_cli.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building redis-cli-proto..."
    DEPENDS proto_generate
)
