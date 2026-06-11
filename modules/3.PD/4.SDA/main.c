#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "middleware.h"
#include <zlog.h>
#include "sda_core.h"
#include "init/sdk_init.h"
#include "intf/intf_api.h"
#include "intf/intf_cb.h"
#include "intf/intf_event.h"
#ifdef SDA_NO_HW
#include "event/sda_event_sim.h"
#endif

/*
 * main.c — SDA 守护进程主程序（三阶段初始化框架）
 *
 * 启动流程:
 *   1. 信号处理注册
 *   2. Phase 1 — 硬件初始化（hw_init_fns[]）
 *   3. Phase 2 — 软件初始化（sw_init_fns[]）
 *   4. Phase 3 — 自定义初始化任务（custom_init_fns[]）
 *   5. 进入事件循环
 *
 * 新增模块只需在对应的数组中挂载函数指针即可。
 */

/* ── 全局上下文 ── */
mw_context_t *g_sda_ctx = NULL;

static volatile int g_running = 1;

/* ── 信号处理 ── */

static void signal_handler(int sig) { (void)sig; g_running = 0; }

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT,  &sa, NULL) < 0) return -1;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

/* ── Phase 2: Redis 连接 ── */

static int sda_sw_redis_connect(mw_context_t *ctx)
{
    (void)ctx;
    g_sda_ctx = mw_connect("127.0.0.1", 6379);
    if (!g_sda_ctx) {
        fprintf(stderr, "sda: Redis connect failed\n");
        return SDA_ERR;
    }
    printf("sda: connected to Redis\n");
    return SDA_OK;
}

/* ══════════════════════════════════════════════════════════
 * 三阶段初始化函数表
 *
 * 新增模块时，在对应数组中添加函数指针即可。
 * 数组以 NULL 哨兵结尾。
 * ══════════════════════════════════════════════════════════ */

/* ── Phase 1: 硬件初始化 ── */
static const hw_init_fn_t hw_init_fns[] = {
#ifndef SDA_NO_HW
    sdk_init_all,         /* SDK 驱动初始化（交换机上电） */
#endif
    NULL
};

/* ── Phase 2: 软件初始化 ── */
static const sw_init_fn_t sw_init_fns[] = {
    sda_sw_redis_connect,     /* Redis 建联 */
    interface_cb_register,    /* 接口事件回调注册 */
    NULL
};

/* ── Phase 3: 自定义初始化任务 ── */
#ifdef SDA_NO_HW
static void sda_sim_init_and_run(mw_context_t *ctx);
#endif

static const custom_init_fn_t custom_init_fns[] = {
    sda_interface_poll_all,   /* 接口信息上报 (NO_HW 时注入初始数据) */
#ifdef SDA_NO_HW
    sda_sim_init_and_run,     /* ★ 注册模拟生成器 + 启动模拟循环（阻塞） */
#endif
    NULL
};

/* ── NO_HW 模拟器启动（阻塞） ── */
#ifdef SDA_NO_HW
static void sda_sim_init_and_run(mw_context_t *ctx)
{
    (void)ctx;
    /* 注册各模块的模拟事件生成器 */
    intf_sim_register_all();
    /* 启动模拟循环（阻塞，替代 mw_poll） */
    sda_sim_run(g_sda_ctx, 1000, &g_running);
}
#endif

/* ══════════════════════════════════════════════════════════
 * 主函数
 * ══════════════════════════════════════════════════════════ */

int main(void)
{
    int i;

    printf("sda: starting (pid=%d)...\n", getpid());

    if (setup_signals() != 0) {
        fprintf(stderr, "sda: signal setup failed\n");
        return 1;
    }

    mw_log_init("sda");
    dzlog_info("sda: starting (pid=%d)", getpid());

    /* ── Phase 1: 硬件初始化 ── */
    printf("sda: phase 1 — hardware init\n");
    for (i = 0; hw_init_fns[i] != NULL; i++) {
        if (hw_init_fns[i]() != SDA_OK) {
            fprintf(stderr, "sda: hw_init[%d] failed\n", i);
            goto fail_hw;
        }
    }

    /* ── Phase 2: 软件初始化 ── */
    printf("sda: phase 2 — software init\n");
    for (i = 0; sw_init_fns[i] != NULL; i++) {
        if (sw_init_fns[i](g_sda_ctx) != SDA_OK) {
            fprintf(stderr, "sda: sw_init[%d] failed\n", i);
            goto fail_sw;
        }
    }

    /* ── Phase 3: 自定义初始化任务 ── */
    printf("sda: phase 3 — custom init tasks\n");
    for (i = 0; custom_init_fns[i] != NULL; i++) {
        /* 自定义任务的失败不阻塞启动 */
        custom_init_fns[i](g_sda_ctx);
    }

    /* ── 事件循环 ── */
#ifdef SDA_NO_HW
    /* NO_HW: 模拟循环已在 Phase 3 的 sda_sim_init_and_run() 中启动 */
    /* （该函数阻塞不返回，不会到达此处） */
#else
    printf("sda: waiting for events...\n");
    while (g_running) {
        mw_poll(g_sda_ctx, 1000);
    }
#endif

    /* ── 正常退出 ── */
    printf("sda: shutting down...\n");
    mw_disconnect(g_sda_ctx);
#ifndef SDA_NO_HW
    sdk_cleanup();
#endif
    printf("sda: stopped\n");
    return 0;

    /* ── 异常退出 ── */
fail_sw:
    if (g_sda_ctx) mw_disconnect(g_sda_ctx);
fail_hw:
#ifndef SDA_NO_HW
    sdk_cleanup();
#endif
    return 1;
}
