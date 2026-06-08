#include <string.h>
#include <stdio.h>

#include "intf/intf_api.h"
#include "sda_core.h"

#ifndef SDA_NO_HW

#include "fm_sdk.h"

static void lane_info_decode(const fm_int *info, int num_lanes,
                              PdInterface__PdInterfaceLaneInfo **lane_ptrs)
{
    for (int i = 0; i < num_lanes; i++) {
        *lane_ptrs[i] = (PdInterface__PdInterfaceLaneInfo)
            PD_INTERFACE__PD_INTERFACE_LANE_INFO__INIT;
        lane_ptrs[i]->rx_pll   = ((uint32_t)info[i] >> 0) & 1;
        lane_ptrs[i]->tx_pll   = ((uint32_t)info[i] >> 1) & 1;
        lane_ptrs[i]->signal   = ((uint32_t)info[i] >> 3) & 0x3;
        lane_ptrs[i]->align    = ((uint32_t)info[i] >> 5) & 1;
        lane_ptrs[i]->auto_det = ((uint32_t)info[i] >> 6) & 1;
        lane_ptrs[i]->mismatch = ((uint32_t)info[i] >> 7) & 1;
    }
}

/*
 * fill_entry — 查询接口状态并填充 proto entry（内部函数）
 *
 * SDK 值与 proto 枚举值一一对应，直接 (EnumType)raw 赋值。
 * 调用前需要预先设置 entry->lanes。
 */
static int fill_entry(int sw, int port, PdInterface__PdInterfaceEntry *entry)
{
    fm_status  st;
    fm_int     mode = 0, port_state = 0, info[4], num_lanes;
    fm_uint32  speed = 0;

    memset(info, 0, sizeof(info));

    st = fmGetPortState((fm_int)sw, (fm_int)port, &mode, &port_state, info);
    if (st != FM_OK) return SDA_ERR;

    st = fmGetNumPortLanes((fm_int)sw, (fm_int)port, FM_PORT_ACTIVE_MAC, &num_lanes);
    if (st != FM_OK) num_lanes = 4;

    fmGetPortAttributeV2((fm_int)sw, (fm_int)port, FM_PORT_ACTIVE_MAC,
                          FM_PORT_LANE_NA, FM_PORT_SPEED, &speed);

    lane_info_decode(info, num_lanes, entry->lanes);

    /* 直接 cast — SDK 值与 proto 枚举值一致 */
    entry->speed      = (PdInterface__PdInterfaceSpeed)speed;
    entry->admin_mode = (PdInterface__PdInterfaceAdminMode)mode;
    entry->link_state = (PdInterface__PdInterfaceLinkState)port_state;
    entry->num_lanes  = (uint32_t)num_lanes;
    entry->n_lanes    = (size_t)num_lanes;

    return SDA_OK;
}

/*
 * sda_interface_update — 查询接口状态并上报 Redis
 */
int sda_interface_update(mw_context_t *ctx, uint32_t sw, uint32_t port)
{
    if (!ctx) return SDA_ERR;

    PdInterface__PdInterfaceKey         key       = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
    PdInterface__PdInterfaceEntry       entry     = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;
    PdInterface__PdInterfaceLaneInfo    lanes[4];
    PdInterface__PdInterfaceLaneInfo   *lane_ptrs[4];

    for (int i = 0; i < 4; i++) lane_ptrs[i] = &lanes[i];
    entry.lanes = lane_ptrs;

    key.sw   = sw;
    key.port = port;

    if (fill_entry((int)sw, (int)port, &entry) != SDA_OK)
        return SDA_ERR;

    return mw_set_message(ctx,
                          (const ProtobufCMessage *)&key,
                          (const ProtobufCMessage *)&entry);
}

/*
 * sda_interface_poll_all — 轮询所有物理接口并写入 Redis
 */
void sda_interface_poll_all(mw_context_t *ctx)
{
    fm_status     st;
    fm_int        sw, next_sw;
    fm_switchInfo info;
    int           count = 0;

    st = fmGetSwitchFirst(&sw);
    while (st == FM_OK && sw >= 0) {
        memset(&info, 0, sizeof(info));
        if (fmGetSwitchInfo(sw, &info) != FM_OK) {
            fprintf(stderr, "sda: fmGetSwitchInfo(%d) failed\n", (int)sw);
        } else {
            printf("sda: polling switch %d (%d ports)...\n",
                   (int)sw, (int)info.numPorts);

            for (fm_int port = 0; port < info.numPorts; port++) {
                if (sda_interface_update(ctx, (uint32_t)sw, (uint32_t)port) == SDA_OK)
                    count++;
            }
        }

        st = fmGetSwitchNext(sw, &next_sw);
        sw = next_sw;
    }

    printf("sda: polled %d interfaces\n", count);
}

#else /* SDA_NO_HW */

/*
 * NO_HW stub: 直接构造测试数据写入 Redis，跳过 SDK 调用。
 *
 * 模拟两台物理接口:
 *   port 0 → SPEED_25G, LINK_UP, ADMIN_UP
 *   port 1 → SPEED_10G, LINK_UP, ADMIN_UP
 */
int sda_interface_update(mw_context_t *ctx, uint32_t sw, uint32_t port)
{
    if (!ctx) return SDA_ERR;

    PdInterface__PdInterfaceKey         key       = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
    PdInterface__PdInterfaceEntry       entry     = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;
    PdInterface__PdInterfaceLaneInfo    lanes[4];
    PdInterface__PdInterfaceLaneInfo   *lane_ptrs[4];

    for (int i = 0; i < 4; i++) {
        lanes[i]    = (PdInterface__PdInterfaceLaneInfo)PD_INTERFACE__PD_INTERFACE_LANE_INFO__INIT;
        lanes[i].rx_pll   = 1;
        lanes[i].tx_pll   = 1;
        lanes[i].signal   = 3;
        lanes[i].align    = 1;
        lane_ptrs[i]      = &lanes[i];
    }
    entry.lanes = lane_ptrs;

    key.sw   = sw;
    key.port = port;
    entry.index      = &key;
    entry.admin_mode = PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP;
    entry.link_state = PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP;
    entry.mtu        = 1500;
    entry.num_lanes  = 4;
    entry.n_lanes    = 4;

    /* 根据 port 编写差异化数据 */
    if (port == 0) {
        entry.port_name = "25G-Port";
        entry.speed     = PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_25G;
        entry.mac_addr  = "02:00:00:00:00:01";
    } else if (port == 1) {
        entry.port_name = "10G-Port";
        entry.speed     = PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_10G;
        entry.mac_addr  = "02:00:00:00:00:02";
    } else {
        return SDA_ERR;
    }

    return mw_set_message(ctx,
                          (const ProtobufCMessage *)&key,
                          (const ProtobufCMessage *)&entry);
}

void sda_interface_poll_all(mw_context_t *ctx)
{
    int count = 0;
    for (uint32_t port = 0; port < 2; port++) {
        if (sda_interface_update(ctx, 0, port) == SDA_OK)
            count++;
    }
    printf("sda: polled %d interfaces (NO_HW)\n", count);
}

#endif /* SDA_NO_HW */
