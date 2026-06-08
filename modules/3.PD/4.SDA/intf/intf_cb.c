#include <stdio.h>
#include <string.h>

#include "intf/intf_cb.h"
#include "sda_core.h"

/* ── 模块级上下文（由 interface_cb_register 设置） ── */
static mw_context_t *g_ctx;

/*
 * interface_cb_register — 注册接口事件回调
 */
int interface_cb_register(mw_context_t *ctx)
{
    g_ctx = ctx;

    mw_callback_entry callbacks[] = {
        /* 待接入回调时在此处添加, 如:
         * { .key_msg = ..., .entry_msg = ..., .cb = on_intf_event },
         */
    };

    /* 数组为空时跳过注册 */
    if ((int)(sizeof(callbacks) / sizeof(callbacks[0])) == 0)
        return SDA_OK;

    return mw_subscribe_keys(ctx, callbacks,
                             sizeof(callbacks) / sizeof(callbacks[0]));
}
