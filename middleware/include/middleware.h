#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <stddef.h>
#include <stdint.h>

/* protobuf-c 前向声明，避免在此处引入完整头文件 */
typedef struct ProtobufCMessage          ProtobufCMessage;
typedef struct ProtobufCMessageDescriptor ProtobufCMessageDescriptor;
typedef struct ProtobufCAllocator        ProtobufCAllocator;

#ifdef __cplusplus
extern "C" {
#endif

/* ── 不透明上下文句柄 ── */
typedef struct mw_context mw_context_t;

/* ── KV 对（用于 hash 操作） ── */
typedef struct {
  const char *field;    /* hash field 名 */
  void       *value;    /* 值（protobuf 二进制） */
  size_t      val_len;  /* 值长度 */
} mw_kv_pair_t;

/* ── Pub/Sub 回调类型 ── */
typedef void (*mw_notify_cb)(const char *event, void *value, void *user_data);

/* ── 连接管理 ── */

/**
 * 连接到 Redis 服务器
 * @host: Redis 地址，默认 "127.0.0.1"
 * @port: Redis 端口，默认 6379
 * @return: 上下文句柄，失败返回 NULL
 */
mw_context_t *mw_connect(const char *host, int port);

/**
 * 断开连接并释放资源
 */
void mw_disconnect(mw_context_t *ctx);

/**
 * 选择 Redis DB
 * @db: DB 编号 (0-15)
 * @return: 0 成功，非 0 失败
 */
int mw_select_db(mw_context_t *ctx, int db);

/* ── Key-Value 操作（protobuf 二进制数据） ── */

/**
 * SET: 写入二进制数据到 key
 * @return: 0 成功，非 0 失败
 */
int mw_set(mw_context_t *ctx, const char *key, const void *data, size_t len);

/**
 * GET: 从 key 读取二进制数据
 * @data: 输出，调用 mw_free_data() 释放
 * @len:  输出长度
 * @return: 0 成功，非 0 失败
 */
int mw_get(mw_context_t *ctx, const char *key, void **data, size_t *len);

/**
 * DEL: 删除 key
 * @return: 0 成功，非 0 失败
 */
int mw_del(mw_context_t *ctx, const char *key);

/**
 * SET message: 将 key_msg 和 val_msg 分别序列化写入
 *
 * Redis key = s_{package}/ + packed(key_msg)
 * package 由 key_msg 的 proto descriptor 自动派生。
 *
 * @key_msg: key 的 protobuf message
 * @val_msg: value 的 protobuf message
 * @return: 0 成功，非 0 失败
 */
int mw_set_message(mw_context_t *ctx,
                   const ProtobufCMessage *key_msg,
                   const ProtobufCMessage *val_msg);

/**
 * GET message: 以 key_msg 二进制定位，读取并反序列化 value
 * @val_desc: value message 的 descriptor（如 &test__light__test_entry__descriptor）
 * @return: 反序列化后的 value message，调用方需用对应 free_unpacked 释放
 */
ProtobufCMessage *mw_get_message(mw_context_t *ctx,
                                 const ProtobufCMessage *key_msg,
                                 const ProtobufCMessageDescriptor *val_desc);

/**
 * DEL message: 以 key_msg 二进制定位，删除 key
 * key 定位由 key_msg 的 descriptor 自动派生，不关心 entry。
 * @return: 0 成功，非 0 失败
 */
int mw_del_message(mw_context_t *ctx,
                   const ProtobufCMessage *key_msg);

/* ── Hash 操作 ── */

/**
 * HSET: 设置 hash 中单个 field 的值
 * @return: 0 成功，非 0 失败
 */
int mw_hset(mw_context_t *ctx, const char *key, const char *field,
            const void *data, size_t len);

/**
 * HGETALL: 获取 hash 中所有 field-value 对
 * @pairs: 输出 kv 数组，调用 mw_free_kv() 释放
 * @count: 输出 kv 数量
 * @return: 0 成功，非 0 失败
 */
int mw_hgetall(mw_context_t *ctx, const char *key,
               mw_kv_pair_t **pairs, int *count);

/**
 * HMUPDATE: 批量更新 hash 的多个 field（HMSET）
 * @pairs: 要设置的 field-value 数组
 * @count: 数组长度
 * @return: 0 成功，非 0 失败
 */
int mw_hmupdate(mw_context_t *ctx, const char *key,
                const mw_kv_pair_t *pairs, int count);

/**
 * HGET: 获取 hash 中单个 field 的值
 * @data: 输出，调用 mw_free_data() 释放
 * @len:  输出长度
 * @return: 0 成功，非 0 失败
 */
int mw_hget(mw_context_t *ctx, const char *key, const char *field,
            void **data, size_t *len);

/* ── Pub/Sub 通知 ── */

/**
 * 回调注册项
 *
 * 组件只需传入 proto key 实例、proto entry 模板实例和回调函数。
 * middleware 自动完成：
 *  - 前缀派生（key_msg->descriptor->package_name → s_{package}/）
 *  - val_desc 提取（entry_msg->descriptor）
 *  - SET 事件时从 Redis GET 并反序列化 value 传给回调
 *  - DEL 事件时 value 传 NULL
 */
typedef struct {
    const ProtobufCMessage *key_msg;   /* proto key 消息指针 */
    const ProtobufCMessage *entry_msg; /* proto entry 模板（仅取 descriptor） */
    mw_notify_cb            cb;        /* 回调函数 */
} mw_callback_entry;

/**
 * 批量注册 keyspace 事件回调
 *
 * middleware 从 key_msg->descriptor->package_name 自动构建 namespace 前缀，
 * 从 entry_msg->descriptor 获取 value 的反序列化描述符。
 */
int mw_subscribe_keys(mw_context_t *ctx,
                      const mw_callback_entry *entries, int count);

/**
 * 轮询 Pub/Sub 消息，驱动回调执行
 *
 * 首次调用时自动完成：订阅连接创建、PSUBSCRIBE 模式构建（基于已注册的 key_prefix）。
 * 之后每次调用读取一条消息，提取 Redis key 并与已注册 key 匹配，触发对应回调。
 *
 * @timeout_ms: 超时毫秒，0=非阻塞，-1=阻塞
 * @return: >0 收到消息数，0 超时/无消息，<0 错误
 */
int mw_poll(mw_context_t *ctx, int timeout_ms);

/* ── 资源释放 ── */

/**
 * 释放 mw_get / mw_hget 返回的数据
 */
void mw_free_data(void *data);

/**
 * 释放 mw_hgetall 返回的 kv 数组
 */
void mw_free_kv(mw_kv_pair_t *pairs, int count);

/**
 * 释放回调中接收到的 value（ProtobufCMessage）
 */
void mw_free_message(ProtobufCMessage *msg);

#ifdef __cplusplus
}
#endif

#endif /* MIDDLEWARE_H */
