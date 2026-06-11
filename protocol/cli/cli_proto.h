#ifndef CLI_PROTO_H
#define CLI_PROTO_H

#include <hiredis/hiredis.h>
#include <sds.h>
#include <sdscompat.h>

/* 初始化 proto 解析 */
void cli_proto_init(void);

/* 对 GET reply 进行 proto 反序列化并输出 */
void cli_proto_format_get(const char *key, const redisReply *reply);

/* 对 MONITOR 整行字符串进行 proto 解析 (type=STATUS reply->str) */
void cli_proto_format_monitor_line(const char *monitor_line);

/* 对 KEYS 返回的单个 key 进行 proto 路径解析并打印 */
void cli_proto_format_key_reply(const redisReply *reply);

/* 将人类可读 proto key (如 s_xx/sw=0,port=1) 编码为二进制 → sds */
sds cli_proto_encode_key(const char *human_key);

/* 将人类可读 proto entry value (如 port_name=25G,speed=SPEED_25G)
 * 编码为二进制 → sds。从 human_key 的前缀匹配注册表获取 entry descriptor。
 * 返回 sds (可能为空) 表示成功，返回 NULL 表示未匹配或解析失败。 */
sds cli_proto_encode_entry_value(const char *human_key, const char *value_fields);

/* 将 glob pattern 中的 proto 路径转为二进制前缀 (丢弃字段过滤) */
sds cli_proto_translate_pattern(const char *pattern);

/* 设置 MONITOR 筛选器（proto 人类可读 pattern，传 NULL 清空） */
void cli_proto_monitor_set_filter(const char *pattern);

#endif
