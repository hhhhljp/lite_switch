#include "sda_event_sim.h"
#include <zlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * sda_event_sim.c — NO_HW 事件模拟框架实现
 */

/* ── 模拟生成器注册表 ── */
static sda_sim_event_fn *g_sim_fns   = NULL;
static int               g_sim_count = 0;
static int               g_sim_cap   = 0;

void sda_sim_register(sda_sim_event_fn fn)
{
    if (!fn) return;

    /* 扩容 */
    if (g_sim_count >= g_sim_cap) {
        int new_cap = g_sim_cap ? g_sim_cap * 2 : 4;
        sda_sim_event_fn *tmp = realloc(g_sim_fns,
                                        (size_t)new_cap * sizeof(sda_sim_event_fn));
        if (!tmp) {
            dzlog_error("sda_sim: realloc failed");
            return;
        }
        g_sim_fns = tmp;
        g_sim_cap = new_cap;
    }

    g_sim_fns[g_sim_count++] = fn;
    dzlog_info("sda_sim: registered generator #%d (%d total)",
               g_sim_count - 1, g_sim_count);
}

void sda_sim_run(mw_context_t *ctx, int interval_ms, volatile int *running)
{
    if (interval_ms <= 0) interval_ms = 1000;

    dzlog_info("sda_sim: starting loop (interval=%dms, generators=%d)",
               interval_ms, g_sim_count);

    struct timespec ts;
    ts.tv_sec  = interval_ms / 1000;
    ts.tv_nsec = (long)(interval_ms % 1000) * 1000000L;

    unsigned long tick = 0;
    while (running == NULL || *running) {
        for (int i = 0; i < g_sim_count; i++) {
            g_sim_fns[i](ctx);
        }

        tick++;
        if (tick % 10 == 0) {
            dzlog_debug("sda_sim: tick=%lu", tick);
        }

        nanosleep(&ts, NULL);
    }
    dzlog_info("sda_sim: stopped (tick=%lu)", tick);
}
