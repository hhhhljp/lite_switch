#include "test/test.pb-c.h"
#include "middleware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mw_context_t *g_ctx;

/* ── proto 静态定义 ── */
static Test__LightTestKey   g_test_key   = TEST__LIGHT__TEST_KEY__INIT;
static Test__LightTestEntry g_test_entry = TEST__LIGHT__TEST_ENTRY__INIT;

/* ── 统一的 set/del 回调 ──
 * set: value 指向 Test__LightTestEntry，key 从 entry->index 获取
 * del: value 为 NULL */
static void on_light_event(const char *event, void *value, void *arg)
{
    (void)arg;

    if (strcmp(event, "set") == 0) {
        Test__LightTestEntry *entry = (Test__LightTestEntry *)value;
        if (entry && entry->index) {
            printf("[SET] test_name=%s  test_id=%u  |  data=%s  data_len=%u\n",
                   entry->index->test_name, entry->index->test_id,
                   entry->data, entry->data_len);
        }
        mw_free_message((ProtobufCMessage *)entry);
    } else if (strcmp(event, "del") == 0) {
        printf("[DEL] Received del event, exiting...\n");
        mw_disconnect(g_ctx);
        exit(0);
    }
}

/* ── main ── */
int main(void)
{
    g_ctx = mw_connect("127.0.0.1", 6379);
    if (!g_ctx) {
        fprintf(stderr, "redis connect failed\n");
        return 1;
    }
    printf("Connected to Redis\n");

    /* 回调注册表 — 使用 g_test_entry 自身承载 descriptor */
    mw_callback_entry callbacks[] = {
        {
            .key_msg   = (const ProtobufCMessage *)&g_test_key,
            .entry_msg = (const ProtobufCMessage *)&g_test_entry,
            .cb        = on_light_event,
        },
    };

    mw_subscribe_keys(g_ctx, callbacks,
                      sizeof(callbacks) / sizeof(callbacks[0]));

    printf("Waiting for events...\n");
    while (1) {
        mw_poll(g_ctx, -1);
    }

    mw_disconnect(g_ctx);
    return 0;
}
