#ifndef SDA_EVENT_SIM_H
#define SDA_EVENT_SIM_H

#include "middleware.h"

/*
 * sda_event_sim.h — NO_HW 事件模拟框架
 *
 * 在 SDA_NO_HW=ON 模式下，没有 SDK 事件源（无 fm_eventPort 等）。
 * 本框架提供周期定时器，按固定间隔调用已注册的模拟事件生成器。
 *
 * 每个生成器直接调用 mw_set_message() 写 Redis —— 这正是事件 handler
 * 中"写 Redis"这一步的替身。上层模块（switch-web、PI 等）通过 Redis
 * Pub/Sub 消费这些模拟事件，与硬件模式完全一致。
 *
 * 用法:
 *   1. 各子模块实现 sda_sim_event_fn，调用 sda_sim_register() 注册
 *   2. Phase 3 最后调用 sda_sim_run() 启动（阻塞，替代 mw_poll 循环）
 */

/* ── 模拟事件生成器签名 ── */
typedef void (*sda_sim_event_fn)(mw_context_t *ctx);

/* ── API ── */

void sda_sim_register(sda_sim_event_fn fn);

void sda_sim_run(mw_context_t *ctx, int interval_ms, volatile int *running);

#endif /* SDA_EVENT_SIM_H */
