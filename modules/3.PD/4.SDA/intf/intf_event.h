#ifndef INTF_EVENT_H
#define INTF_EVENT_H

/*
 * intf_event.h — intf 模块事件处理声明
 *
 * 声明 intf 模块对外提供的事件相关 API：
 *   - intf_sim_register_all: 注册 NO_HW 模拟事件生成器
 */

#ifdef SDA_NO_HW

/** 注册 intf 模块的所有 NO_HW 模拟事件生成器 */
void intf_sim_register_all(void);

#endif /* SDA_NO_HW */

#endif /* INTF_EVENT_H */
