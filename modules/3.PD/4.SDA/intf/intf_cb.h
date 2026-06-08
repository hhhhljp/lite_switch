#ifndef INTF_CB_H
#define INTF_CB_H

#include "middleware.h"

/*
 * intf_cb.h — intf 模块回调管理
 *
 * 提供本模块所有 keyspace 回调的注册接口。
 * 作为 sw_init_fn_t 挂载到 main.c 的 sw_init_fns 数组中。
 */

/*
 * interface_cb_register — 注册 interface 模块的所有回调
 *
 * 将 PdInterfaceKey/PdInterfaceEntry 的 set/del 事件回调
 * 注册到 middleware。ctx 由调用方传入（即 sw_init 阶段）。
 *
 * @return: SDA_OK (0) 成功，SDA_ERR (-1) 失败
 */
int interface_cb_register(mw_context_t *ctx);

#endif /* INTF_CB_H */
