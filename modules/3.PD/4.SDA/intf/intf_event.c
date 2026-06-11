#ifdef SDA_NO_HW

#include "intf_event.h"
#include "event/sda_event_sim.h"
#include "PD/interface/interface.pb-c.h"
#include <zlog.h>
#include <stdio.h>

/*
 * intf_event.c — intf 模块事件处理
 *
 * NO_HW 模式：提供模拟 link toggle 生成器，每秒翻转 port 0 的 link_state。
 * 上层模块（switch-web 等）通过 Redis Pub/Sub 感知变化，无需手动注入。
 */

static void intf_sim_link_toggle(mw_context_t *ctx)
{
    static int toggle = 0;
    toggle = !toggle;

    PdInterface__PdInterfaceKey   key   = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
    PdInterface__PdInterfaceEntry entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;

    key.sw   = 0;
    key.port = 0;

    entry.index      = &key;
    entry.port_name  = (char *)"sim-port";
    entry.speed      = PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_25G;
    entry.admin_mode = PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP;
    entry.link_state = toggle
        ? PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP
        : PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN;
    entry.mtu        = 1500;
    entry.mac_addr   = (char *)"02:00:00:00:00:01";
    entry.num_lanes  = 4;
    entry.n_lanes    = 0;

    dzlog_debug("sim: port 0 link %s", toggle ? "UP" : "DOWN");

    mw_set_message(ctx,
                   (const ProtobufCMessage *)&key,
                   (const ProtobufCMessage *)&entry);
}

void intf_sim_register_all(void)
{
    sda_sim_register(intf_sim_link_toggle);
}

#endif /* SDA_NO_HW */
