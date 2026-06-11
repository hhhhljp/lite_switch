#ifndef SDA_CORE_H
#define SDA_CORE_H

#include "middleware.h"
#include "sda_event.h"

/*
 * sda_core.h — SDA 模块核心定义
 *
 * 定义全局常量、初始化函数类型和三阶段初始化框架。
 */

/* ── 交换机配置 ── */

#define SDA_NUM_SWITCHES  1

/* ── 通用状态码 ── */

enum sda_status {
    SDA_OK  =  0,
    SDA_ERR = -1
};

/* ── 三阶段初始化函数类型 ── */

/*
 * 硬件初始化（无 ctx 依赖）
 *   交换机上电、SDK 驱动加载等
 */
typedef int (*hw_init_fn_t)(void);

/*
 * 软件初始化（ctx 已就绪）
 *   Redis 连接、回调注册、Pub/Sub 订阅等
 */
typedef int (*sw_init_fn_t)(mw_context_t *ctx);

/*
 * 自定义初始化任务（ctx 已就绪）
 *   接口信息上报、初始状态同步等。
 *   失败不阻塞启动流程，返回值被忽略。
 */
typedef void (*custom_init_fn_t)(mw_context_t *ctx);

/* ── 全局上下文 ── */

extern mw_context_t *g_sda_ctx;

#endif /* SDA_CORE_H */
