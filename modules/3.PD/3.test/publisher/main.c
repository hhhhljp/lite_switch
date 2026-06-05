#include "test/test.pb-c.h"
#include "middleware.h"
#include <stdio.h>
#include <unistd.h>

/* ── 数据初始化：构造一个 Test__LightTestEntry ── */
static void entry_init(Test__LightTestKey *key, Test__LightTestEntry *entry)
{
  Test__LightTestKey k = TEST__LIGHT__TEST_KEY__INIT;
  k.test_name = "lightkey";
  k.test_id   = 100;
  *key = k;

  Test__LightTestEntry e = TEST__LIGHT__TEST_ENTRY__INIT;
  e.index     = key;
  e.test_name = "lightentry";
  e.data      = "hello world";
  e.data_len  = sizeof("hello world");
  *entry = e;
}

int main(void)
{
  Test__LightTestKey   key_body;
  Test__LightTestEntry entry;

  /* 1. 初始化数据 */
  entry_init(&key_body, &entry);

  /* 2. 连接 Redis */
  mw_context_t *ctx = mw_connect("127.0.0.1", 6379);
  if (!ctx) {
    fprintf(stderr, "redis connect failed\n");
    return 1;
  }

  /* 3. SET */
  printf("SET ...\n");
  mw_set_message(ctx,
                 (const ProtobufCMessage *)&key_body,
                 (const ProtobufCMessage *)&entry);

  /* 4. sleep 3s */
  sleep(3);

  /* 5. DEL */
  printf("DEL ...\n");
  mw_del_message(ctx,
                 (const ProtobufCMessage *)&key_body);

  /* 6. 断开连接 */
  mw_disconnect(ctx);
  return 0;
}
