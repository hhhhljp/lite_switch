#ifndef SDA_EVENT_H
#define SDA_EVENT_H

#include "middleware.h"

/*
 * sda_event.h — SDA 事件路由框架公共头文件
 *
 * 当前版本（NO_HW 优先实现）仅提供事件 handler 函数签名。
 * 完整的事件路由表和模块自注册机制将在后续 HW 阶段实现。
 */

/* ── 事件处理函数签名 ── */
typedef void (*sda_event_fn)(mw_context_t *ctx, int sw, void *payload);

/* ── 宏：声明模块的事件 handler（放在子模块的 *_event.h 中） ── */
#define SDA_EVENT_HANDLER(_event, _func) \
    void _func(mw_context_t *ctx, int sw, void *payload)

#endif /* SDA_EVENT_H */
