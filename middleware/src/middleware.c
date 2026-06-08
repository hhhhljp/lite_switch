#include "middleware.h"
#include <hiredis/hiredis.h>
#include <protobuf-c/protobuf-c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 内部结构 ── */

typedef struct {
  char            *key_prefix;  /* namespace 前缀，如 "s_test/" */
  uint8_t         *key_packed;  /* 打包后的 proto key 二进制 */
  size_t           key_len;     /* 打包长度 */
  const ProtobufCMessageDescriptor *key_desc;  /* key descriptor */
  const ProtobufCMessageDescriptor *val_desc;  /* value descriptor（反序列化用） */
  mw_notify_cb     cb;          /* 回调函数 */
} mw_cb_entry;

struct mw_context {
  redisContext  *conn;       /* Redis 同步连接 */
  redisContext  *sub_conn;   /* Pub/Sub 专用连接（独立，延迟创建） */
  mw_cb_entry   *callbacks;  /* 回调注册表（动态数组） */
  int            cb_count;   /* 已注册数量 */
  int            cb_cap;     /* 数组容量 */
};

/* ── 连接管理 ── */

mw_context_t *mw_connect(const char *host, int port)
{
  if (!host) host = "127.0.0.1";
  if (port <= 0) port = 6379;

  mw_context_t *ctx = calloc(1, sizeof(mw_context_t));
  if (!ctx) return NULL;

  ctx->conn = redisConnect(host, port);
  if (!ctx->conn || ctx->conn->err) {
    free(ctx);
    return NULL;
  }

  return ctx;
}

void mw_disconnect(mw_context_t *ctx)
{
  if (!ctx) return;
  if (ctx->conn)  redisFree(ctx->conn);
  if (ctx->sub_conn) redisFree(ctx->sub_conn);
  for (int i = 0; i < ctx->cb_count; i++) {
    free(ctx->callbacks[i].key_prefix);
    free(ctx->callbacks[i].key_packed);
  }
  free(ctx->callbacks);
  free(ctx);
}

int mw_select_db(mw_context_t *ctx, int db)
{
  if (!ctx || !ctx->conn) return -1;

  redisReply *reply = redisCommand(ctx->conn, "SELECT %d", db);
  if (!reply) return -1;

  int ok = (reply->type == REDIS_REPLY_STATUS &&
            strcmp(reply->str, "OK") == 0) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

/* ── Key-Value 操作 ── */

int mw_set(mw_context_t *ctx, const char *key, const void *data, size_t len)
{
  if (!ctx || !ctx->conn || !key || !data) return -1;

  redisReply *reply = redisCommand(ctx->conn, "SET %s %b", key, data, len);
  if (!reply) return -1;

  int ok = (reply->type == REDIS_REPLY_STATUS &&
            strcmp(reply->str, "OK") == 0) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

int mw_get(mw_context_t *ctx, const char *key, void **data, size_t *len)
{
  if (!ctx || !ctx->conn || !key || !data || !len) return -1;

  *data = NULL;
  *len  = 0;

  redisReply *reply = redisCommand(ctx->conn, "GET %s", key);
  if (!reply) return -1;

  if (reply->type == REDIS_REPLY_NIL) {
    freeReplyObject(reply);
    return -1;
  }
  if (reply->type != REDIS_REPLY_STRING) {
    freeReplyObject(reply);
    return -1;
  }

  *data = malloc(reply->len);
  memcpy(*data, reply->str, reply->len);
  *len = reply->len;

  freeReplyObject(reply);
  return 0;
}

int mw_del(mw_context_t *ctx, const char *key)
{
  if (!ctx || !ctx->conn || !key) return -1;

  redisReply *reply = redisCommand(ctx->conn, "DEL %s", key);
  if (!reply) return -1;

  int ok = (reply->type == REDIS_REPLY_INTEGER) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

/* ── Protobuf message 操作 ── */

/*
 * 构建 namespace 前缀: "s_{package}/"
 * 例如 package="test" → "s_test/"
 */
static char *mw_make_prefix(const ProtobufCMessage *key_msg)
{
  if (!key_msg || !key_msg->descriptor) return NULL;

  const char *pkg = key_msg->descriptor->package_name;
  if (!pkg || !pkg[0]) return NULL;

  size_t len = 2 + strlen(pkg) + 1 + 1;
  char *buf = malloc(len);
  if (!buf) return NULL;
  snprintf(buf, len, "s_%s/", pkg);
  return buf;
}

/* 构建 Redis key: prefix + packed(key_msg) */
static void *key_msg_build(const ProtobufCMessage *key_msg,
                           size_t *out_len)
{
  char  *prefix = mw_make_prefix(key_msg);
  size_t plen   = prefix ? strlen(prefix) : 0;
  size_t klen   = protobuf_c_message_get_packed_size(key_msg);
  *out_len = plen + klen;

  uint8_t *buf = malloc(*out_len);
  if (plen) memcpy(buf, prefix, plen);
  protobuf_c_message_pack(key_msg, buf + plen);
  free(prefix);
  return buf;
}

int mw_set_message(mw_context_t *ctx,
                   const ProtobufCMessage *key_msg,
                   const ProtobufCMessage *val_msg)
{
  if (!ctx || !ctx->conn || !key_msg || !val_msg) return -1;

  size_t key_len;
  void  *key_buf = key_msg_build(key_msg, &key_len);

  size_t   val_len = protobuf_c_message_get_packed_size(val_msg);
  uint8_t *val_buf = malloc(val_len);
  protobuf_c_message_pack(val_msg, val_buf);

  redisReply *reply = redisCommand(ctx->conn, "SET %b %b",
                                   key_buf, key_len,
                                   val_buf, val_len);
  free(key_buf);
  free(val_buf);

  if (!reply) return -1;
  int ok = (reply->type == REDIS_REPLY_STATUS &&
            strcmp(reply->str, "OK") == 0) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

ProtobufCMessage *mw_get_message(mw_context_t *ctx,
                                 const ProtobufCMessage *key_msg,
                                 const ProtobufCMessageDescriptor *val_desc)
{
  if (!ctx || !ctx->conn || !key_msg || !val_desc) return NULL;

  size_t key_len;
  void  *key_buf = key_msg_build(key_msg, &key_len);

  redisReply *reply = redisCommand(ctx->conn, "GET %b", key_buf, key_len);
  free(key_buf);

  if (!reply || reply->type != REDIS_REPLY_STRING) {
    if (reply) freeReplyObject(reply);
    return NULL;
  }

  ProtobufCMessage *msg = protobuf_c_message_unpack(val_desc, NULL,
                                                     reply->len,
                                                     (uint8_t *)reply->str);
  freeReplyObject(reply);
  return msg;
}

int mw_del_message(mw_context_t *ctx,
                   const ProtobufCMessage *key_msg)
{
  if (!ctx || !ctx->conn || !key_msg) return -1;

  size_t key_len;
  void  *key_buf = key_msg_build(key_msg, &key_len);

  redisReply *reply = redisCommand(ctx->conn, "DEL %b", key_buf, key_len);
  free(key_buf);

  if (!reply) return -1;
  int ok = (reply->type == REDIS_REPLY_INTEGER) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

/* ── Hash 操作 ── */

int mw_hset(mw_context_t *ctx, const char *key, const char *field,
            const void *data, size_t len)
{
  if (!ctx || !ctx->conn || !key || !field || !data) return -1;

  redisReply *reply = redisCommand(ctx->conn, "HSET %s %s %b",
                                   key, field, data, len);
  if (!reply) return -1;

  int ok = (reply->type == REDIS_REPLY_INTEGER) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

int mw_hgetall(mw_context_t *ctx, const char *key,
               mw_kv_pair_t **pairs, int *count)
{
  if (!ctx || !ctx->conn || !key || !pairs || !count) return -1;

  *pairs = NULL;
  *count = 0;

  redisReply *reply = redisCommand(ctx->conn, "HGETALL %s", key);
  if (!reply || reply->type != REDIS_REPLY_ARRAY) {
    if (reply) freeReplyObject(reply);
    return -1;
  }

  int n = reply->elements / 2;
  mw_kv_pair_t *kv = calloc(n, sizeof(mw_kv_pair_t));
  for (int i = 0; i < n; i++) {
    const char *f = reply->element[i * 2]->str;
    const char *v = reply->element[i * 2 + 1]->str;
    size_t      vl = reply->element[i * 2 + 1]->len;

    kv[i].field = strdup(f);
    kv[i].value = malloc(vl);
    memcpy(kv[i].value, v, vl);
    kv[i].val_len = vl;
  }

  *pairs = kv;
  *count = n;

  freeReplyObject(reply);
  return 0;
}

int mw_hmupdate(mw_context_t *ctx, const char *key,
                const mw_kv_pair_t *pairs, int count)
{
  if (!ctx || !ctx->conn || !key || !pairs || count <= 0) return -1;

  int argc = 2 + count * 3;
  const char **argv = calloc(argc, sizeof(char *));
  size_t *argvlen  = calloc(argc, sizeof(size_t));

  argv[0] = "HMSET";
  argvlen[0] = 5;
  argv[1] = key;
  argvlen[1] = strlen(key);

  for (int i = 0; i < count; i++) {
    argv[2 + i * 3]     = pairs[i].field;
    argvlen[2 + i * 3]  = strlen(pairs[i].field);
    argv[2 + i * 3 + 1] = pairs[i].value;
    argvlen[2 + i * 3 + 1] = pairs[i].val_len;
  }

  redisReply *reply = redisCommandArgv(ctx->conn, argc, argv, argvlen);
  free(argv);
  free(argvlen);

  if (!reply) return -1;

  int ok = (reply->type == REDIS_REPLY_STATUS &&
            strcmp(reply->str, "OK") == 0) ? 0 : -1;
  freeReplyObject(reply);
  return ok;
}

int mw_hget(mw_context_t *ctx, const char *key, const char *field,
            void **data, size_t *len)
{
  if (!ctx || !ctx->conn || !key || !field || !data || !len) return -1;

  *data = NULL;
  *len  = 0;

  redisReply *reply = redisCommand(ctx->conn, "HGET %s %s", key, field);
  if (!reply) return -1;

  if (reply->type == REDIS_REPLY_NIL) {
    freeReplyObject(reply);
    return -1;
  }
  if (reply->type != REDIS_REPLY_STRING) {
    freeReplyObject(reply);
    return -1;
  }

  *data = malloc(reply->len);
  memcpy(*data, reply->str, reply->len);
  *len = reply->len;

  freeReplyObject(reply);
  return 0;
}

/* ── Pub/Sub ── */

int mw_subscribe_keys(mw_context_t *ctx,
                      const mw_callback_entry *entries, int count)
{
  if (!ctx || !entries || count <= 0) return -1;

  for (int i = 0; i < count; i++) {
    const mw_callback_entry *in = &entries[i];
    if (!in->key_msg || !in->entry_msg || !in->cb) return -1;

    /* 扩容 */
    if (ctx->cb_count >= ctx->cb_cap) {
      int new_cap = ctx->cb_cap ? ctx->cb_cap * 2 : 4;
      mw_cb_entry *tmp = realloc(ctx->callbacks,
                                 (size_t)new_cap * sizeof(mw_cb_entry));
      if (!tmp) return -1;
      ctx->callbacks = tmp;
      ctx->cb_cap = new_cap;
    }

    size_t klen = protobuf_c_message_get_packed_size(in->key_msg);
    uint8_t *packed = malloc(klen);
    if (!packed) return -1;
    protobuf_c_message_pack(in->key_msg, packed);

    mw_cb_entry *e = &ctx->callbacks[ctx->cb_count];
    e->key_prefix = mw_make_prefix(in->key_msg);
    e->key_packed = packed;
    e->key_len    = klen;
    e->key_desc   = in->key_msg->descriptor;
    e->val_desc   = in->entry_msg->descriptor;
    e->cb         = in->cb;
    ctx->cb_count++;
  }

  return 0;
}

/* ── 内部：从 keyspace channel 提取 Redis key ── */
static const char *extract_redis_key(const char *channel)
{
  const char *p = strrchr(channel, ':');
  return p ? p + 1 : channel;
}

/* ── 内部：延迟订阅 ── */
static int mw_subscribe_lazy(mw_context_t *ctx)
{
  if (!ctx->conn) return -1;

  int port = ctx->conn->tcp.port;
  const char *host = ctx->conn->tcp.host;

  ctx->sub_conn = redisConnect(host, port);
  if (!ctx->sub_conn || ctx->sub_conn->err) return -1;

  /* 收集所有唯一前缀，构建 PSUBSCRIBE 参数 */
  const char **patterns = calloc((size_t)ctx->cb_count, sizeof(char *));
  int npat = 0;

  for (int i = 0; i < ctx->cb_count; i++) {
    const char *pfx = ctx->callbacks[i].key_prefix;
    if (!pfx) continue;
    /* 去重 */
    int dup = 0;
    for (int j = 0; j < npat; j++) {
      if (strcmp(patterns[j], pfx) == 0) { dup = 1; break; }
    }
    if (dup) continue;

    /* 拼接 "__keyspace@*__:<pfx>*" */
    size_t len = strlen(pfx) + 18;
    char *pat = malloc(len);
    snprintf(pat, len, "__keyspace@*__:%s*", pfx);
    patterns[npat++] = pat;
  }

  if (npat > 0) {
    int argc = 1 + npat;
    const char **argv = calloc((size_t)argc, sizeof(char *));
    size_t *argvlen    = calloc((size_t)argc, sizeof(size_t));
    argv[0] = "PSUBSCRIBE";
    argvlen[0] = 10;
    for (int i = 0; i < npat; i++) {
      argv[1 + i] = patterns[i];
      argvlen[1 + i] = strlen(patterns[i]);
    }
    redisReply *r = redisCommandArgv(ctx->sub_conn, argc, argv, argvlen);
    if (r) freeReplyObject(r);
    free(argv);
    free(argvlen);
  }

  for (int i = 0; i < npat; i++) free((void *)patterns[i]);
  free(patterns);

  return 0;
}

/*
 * 通用 poll 处理：读取一条 Pub/Sub 消息，匹配并触发回调
 * SET 事件时通过 Redis GET 获取 value 并反序列化后传入，DEL 事件 value 为 NULL。
 */
static int mw_poll_one(mw_context_t *ctx)
{
  redisReply *reply = NULL;
  if (redisGetReply(ctx->sub_conn, (void **)&reply) != REDIS_OK)
    return -1;
  if (!reply) return 0;

  if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 4) {
    const char *channel = reply->element[2]->str;
    const char *event   = reply->element[3]->str;
    const char *rkey    = extract_redis_key(channel);

    for (int i = 0; i < ctx->cb_count; i++) {
      mw_cb_entry *e = &ctx->callbacks[i];
      if (!e->key_prefix) continue;
      size_t plen = strlen(e->key_prefix);

      if (strncmp(rkey, e->key_prefix, plen) != 0) continue;
      if (memcmp(rkey + plen, e->key_packed, e->key_len) != 0) continue;

      /* SET 事件：从 Redis 获取 value 并反序列化 */
      void *value = NULL;
      if (strcmp(event, "set") == 0) {
        redisReply *gr = redisCommand(ctx->conn, "GET %s", rkey);
        if (gr && gr->type == REDIS_REPLY_STRING) {
          value = protobuf_c_message_unpack(e->val_desc, NULL,
                                            gr->len, (uint8_t *)gr->str);
          /* 从 Redis key 中解析 key 子消息，重建 entry->index
           *（避免 value 中存储冗余 index，index 始终与 key 对齐） */
          if (value && e->key_desc && e->key_prefix) {
            const char  *channel    = reply->element[2]->str;
            size_t       channel_len = reply->element[2]->len;
            size_t       rkey_off    = (size_t)(rkey - channel);
            size_t       plen        = strlen(e->key_prefix);
            size_t       klen        = channel_len - rkey_off - plen;
            const char  *kdata       = rkey + plen;
            ProtobufCMessage *kmsg = protobuf_c_message_unpack(
                e->key_desc, NULL, klen, (const uint8_t *)kdata);
            if (kmsg) {
              const ProtobufCMessageDescriptor *vd = e->val_desc;
              for (unsigned j = 0; j < vd->n_fields; j++) {
                const ProtobufCFieldDescriptor *fd = &vd->fields[j];
                if (fd->type == PROTOBUF_C_TYPE_MESSAGE
                    && strcmp(fd->name, "index") == 0) {
                  uint8_t *base = (uint8_t *)value;
                  void **ptr = (void **)(base + fd->offset);
                  if (*ptr)
                    protobuf_c_message_free_unpacked((ProtobufCMessage *)*ptr, NULL);
                  *ptr = kmsg;
                  if (fd->label == PROTOBUF_C_LABEL_OPTIONAL)
                    *(protobuf_c_boolean *)(base + fd->quantifier_offset) = 1;
                  break;
                }
              }
            }
          }
        }
        if (gr) freeReplyObject(gr);
      }

      e->cb(event, value, e);
    }
  }
  freeReplyObject(reply);
  return 1;
}

int mw_poll(mw_context_t *ctx, int timeout_ms)
{
  if (!ctx) return -1;

  /* 首次 poll 时延迟订阅 */
  if (!ctx->sub_conn) {
    if (mw_subscribe_lazy(ctx) != 0) return -1;
  }

  if (timeout_ms == 0) {
    return mw_poll_one(ctx);
  }

  if (timeout_ms < 0) {
    return mw_poll_one(ctx);
  }

  /* 带超时 */
  struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
  redisSetTimeout(ctx->sub_conn, tv);

  return mw_poll_one(ctx);
}

/* ── 资源释放 ── */

void mw_free_data(void *data)
{
  free(data);
}

void mw_free_message(ProtobufCMessage *msg)
{
  if (msg)
    protobuf_c_message_free_unpacked(msg, NULL);
}

void mw_free_kv(mw_kv_pair_t *pairs, int count)
{
  if (!pairs) return;
  for (int i = 0; i < count; i++) {
    free((void *)pairs[i].field);
    free(pairs[i].value);
  }
  free(pairs);
}
