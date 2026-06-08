#ifndef INTF_API_H
#define INTF_API_H

#include "PD/interface/interface.pb-c.h"
#include "middleware.h"

/*
 * intf_api.h — 接口管理模块
 *
 * 提供物理接口状态查询和轮询上报的 SDK 操作。
 * 所有通信通过 proto entry 完成，不做字符串转换。
 */

/*
 * sda_interface_update — 查询接口状态并上报 Redis
 *
 * 为 sw:port 创建 key/entry，调用 SDK 填充接口状态，通过 mw_set_message 写回 Redis。
 * 这是接口状态更新的标准化入口，封装了 key/entry 初始化、lane 缓冲区管理、
 * fill_entry 调用和 Redis 上报的完整流程。
 *
 * mw_set_message 是同步阻塞的（内部 protobuf pack 深拷贝后返回），
 * 故所有局部变量在函数返回后自动安全销毁。
 *
 * @return: SDA_OK 成功，SDA_ERR 失败
 */
int sda_interface_update(mw_context_t *ctx, uint32_t sw, uint32_t port);

/*
 * sda_interface_poll_all — 轮询所有物理接口并写入 Redis
 *
 * 遍历所有交换机所有端口，逐个调用 sda_interface_update 上报。
 */
void sda_interface_poll_all(mw_context_t *ctx);

#endif /* INTF_API_H */
