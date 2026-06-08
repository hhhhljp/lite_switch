#include "PD/interface/interface.pb-c.h"
#include "middleware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mw_context_t *g_ctx;

/* ── proto 静态定义 （INIT = 全默认值，packed 0 字节 → 匹配全部 s_pd_interface/*）── */
static PdInterface__PdInterfaceKey   g_pd_key   = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
static PdInterface__PdInterfaceEntry g_pd_entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;

/* ── 回调 ── */
static void on_pd_event(const char *event, void *value, void *arg)
{
    (void)arg;

    if (strcmp(event, "set") == 0) {
        PdInterface__PdInterfaceEntry *entry = (PdInterface__PdInterfaceEntry *)value;
        if (entry && entry->index) {
            printf("[SET] key: sw=%u  port=%u  |  "
                   "entry: port_name=%s  speed=%d  admin=%d  link=%d  mtu=%u  mac=%s\n",
                   entry->index->sw, entry->index->port,
                   entry->port_name ? entry->port_name : "",
                   (int)entry->speed,
                   (int)entry->admin_mode,
                   (int)entry->link_state,
                   entry->mtu,
                   entry->mac_addr ? entry->mac_addr : "");
        } else {
            printf("[SET] MISSING index! entry=%p\n", (void *)entry);
        }
        mw_free_message((ProtobufCMessage *)entry);
    } else if (strcmp(event, "del") == 0) {
        printf("[DEL] exiting...\n");
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

    mw_callback_entry callbacks[] = {
        {
            .key_msg   = (const ProtobufCMessage *)&g_pd_key,
            .entry_msg = (const ProtobufCMessage *)&g_pd_entry,
            .cb        = on_pd_event,
        },
    };

    mw_subscribe_keys(g_ctx, callbacks,
                      sizeof(callbacks) / sizeof(callbacks[0]));

    printf("Waiting for PdInterface events...\n");
    while (1) {
        mw_poll(g_ctx, -1);
    }

    mw_disconnect(g_ctx);
    return 0;
}
