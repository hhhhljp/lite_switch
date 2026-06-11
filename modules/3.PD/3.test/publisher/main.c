#include "PD/interface/interface.pb-c.h"
#include "test/test.pb-c.h"
#include "middleware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void inject_interface(mw_context_t *ctx, uint32_t sw, uint32_t port,
                              const char *name, PdInterface__PdInterfaceSpeed speed,
                              PdInterface__PdInterfaceAdminMode admin,
                              PdInterface__PdInterfaceLinkState link,
                              uint32_t mtu, const char *mac, uint32_t lanes)
{
    PdInterface__PdInterfaceKey key = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
    key.sw   = sw;
    key.port = port;

    PdInterface__PdInterfaceLaneInfo lane_info = PD_INTERFACE__PD_INTERFACE_LANE_INFO__INIT;
    lane_info.rx_pll   = 1;
    lane_info.tx_pll   = 1;
    lane_info.signal   = 3;
    lane_info.align    = 1;
    lane_info.auto_det = 1;
    lane_info.mismatch = 0;

    PdInterface__PdInterfaceLaneInfo *lane_arr[1] = { &lane_info };

    PdInterface__PdInterfaceEntry entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;
    entry.index      = &key;
    entry.port_name  = (char *)name;
    entry.speed      = speed;
    entry.admin_mode = admin;
    entry.link_state = link;
    entry.mtu        = mtu;
    entry.mac_addr   = (char *)mac;
    entry.num_lanes  = lanes;
    entry.n_lanes    = 1;
    entry.lanes      = lane_arr;

    int rc = mw_set_message(ctx,
                            (const ProtobufCMessage *)&key,
                            (const ProtobufCMessage *)&entry);
    printf("[inject] sw=%u port=%u name=%-8s speed=%u admin=%d link=%d mtu=%u -> %s\n",
           sw, port, name, (unsigned)speed, (int)admin, (int)link, mtu,
           rc == 0 ? "OK" : "FAIL");
}

int main(void)
{
    mw_context_t *ctx = mw_connect("127.0.0.1", 6379);
    if (!ctx) { fprintf(stderr, "Redis connect failed\n"); return 1; }
    printf("Connected to Redis\n");

    inject_interface(ctx, 0, 1, "port-01", PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_10G,
                     PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP,
                     PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN,
                     1500, "00:11:22:33:44:55", 1);
    inject_interface(ctx, 0, 2, "port-02", PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_25G,
                     PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_DOWN,
                     PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN,
                     1500, "00:22:33:44:55:66", 1);
    inject_interface(ctx, 0, 3, "port-03", PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_40G,
                     PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP,
                     PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP,
                     9000, "00:33:44:55:66:77", 2);
    inject_interface(ctx, 0, 4, "port-04", PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_100G,
                     PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP,
                     PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP,
                     1500, "00:44:55:66:77:88", 1);

    printf("Done. 4 interfaces injected.\n");
    mw_disconnect(ctx);
    return 0;
}
