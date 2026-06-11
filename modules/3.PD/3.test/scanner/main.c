#include "PD/interface/interface.pb-c.h"
#include "middleware.h"
#include <zlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PdInterface__PdInterfaceKey   g_pd_key   = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
static PdInterface__PdInterfaceEntry g_pd_entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;

static int g_found = 0;

static void on_pd_event(const char *event, void *value, void *user_data)
{
    (void)user_data;

    if (strcmp(event, "set") == 0) {
        PdInterface__PdInterfaceEntry *entry = (PdInterface__PdInterfaceEntry *)value;
        if (entry && entry->index) {
            g_found++;
            dzlog_info( "[%d] sw=%u port=%u name=%s speed=%u admin=%d link=%d mtu=%u",
                      g_found, entry->index->sw, entry->index->port,
                      entry->port_name ? entry->port_name : "(null)",
                      (unsigned)entry->speed, (int)entry->admin_mode,
                      (int)entry->link_state, entry->mtu);
            printf("[%d] sw=%-3u port=%-3u | %-8s  speed=%-6u  admin=%-11s  link=%-15s  mtu=%-5u  mac=%s\n",
                   g_found,
                   entry->index->sw, entry->index->port,
                   entry->port_name ? entry->port_name : "(null)",
                   (unsigned)entry->speed,
                   entry->admin_mode == PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP   ? "UP" :
                   entry->admin_mode == PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_DOWN ? "DOWN" : "?",
                   entry->link_state == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP           ? "UP" :
                   entry->link_state == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN         ? "DOWN" :
                   entry->link_state == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_LOCAL_FAULT  ? "LOCAL_FAULT" :
                   entry->link_state == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_REMOTE_FAULT ? "REMOTE_FAULT" : "?",
                   entry->mtu,
                   entry->mac_addr ? entry->mac_addr : "(null)");
        } else {
            dzlog_warn( "entry->index is NULL, skipped");
        }
        if (entry) mw_free_message((ProtobufCMessage *)entry);
    } else if (strcmp(event, "del") == 0) {
        dzlog_debug( "DEL event received");
    }
}

int main(void)
{
    /* ── 初始化 zlog 日志 ── */
    if (mw_log_init("scanner") != 0) {
        fprintf(stderr, "zlog init failed\n");
        return 1;
    }
    dzlog_info( "scanner starting");

    mw_context_t *ctx = mw_connect("127.0.0.1", 6379);
    if (!ctx) {
        dzlog_error( "Redis connect failed");
        return 1;
    }
    dzlog_info( "Connected to Redis");
    printf("Connected to Redis\n");

    /* 注册回调 */
    mw_callback_entry callbacks[] = {
        {
            .key_msg   = (const ProtobufCMessage *)&g_pd_key,
            .entry_msg = (const ProtobufCMessage *)&g_pd_entry,
            .cb        = on_pd_event,
        },
    };
    mw_subscribe_keys(ctx, callbacks,
                      sizeof(callbacks) / sizeof(callbacks[0]));

    /* 扫描存量 */
    mw_scan_entry scan_entries[] = {
        {
            .key_msg   = (const ProtobufCMessage *)&g_pd_key,
            .entry_msg = (const ProtobufCMessage *)&g_pd_entry,
        },
    };
    printf("Scanning for PdInterface entries ...\n");
    int n = mw_scan(ctx, scan_entries,
                    sizeof(scan_entries) / sizeof(scan_entries[0]));
    dzlog_info( "mw_scan enqueued %d event(s)", n);
    printf("mw_scan enqueued %d event(s)\n", n);

    /* poll 循环 */
    int expected = n;
    int received = 0;
    while (received < expected) {
        if (mw_poll(ctx, 1000) > 0) received++;
    }

    dzlog_info( "scan complete: %d interface(s) found", g_found);
    printf("\n=== Scan complete: %d interface(s) found ===\n", g_found);

    mw_disconnect(ctx);
    dzlog_info( "scanner exiting");
    return 0;
}
