#!/usr/bin/env python3
"""Patch redis-cli.c to add --proto support."""
import sys

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} path/to/redis-cli.c")
    sys.exit(1)

with open(sys.argv[1]) as f:
    text = f.read()

# 1. Add include
text = text.replace(
    '#include "hdr_histogram.h"',
    '#include "hdr_histogram.h"\n#include "cli_proto.h"'
)

# 2. Add proto fields to struct config
text = text.replace(
    "    int monitor_mode;\n    int pubsub_mode;",
    "    int monitor_mode;\n    int proto_mode;\n    int proto_keys_mode;\n    sds proto_get_key;\n    sds proto_set_key;\n    int pubsub_mode;"
)

# 3. Replace cliReadReply output block
old = """    if (output) {
        out = cliFormatReply(reply, config.output, output_raw_strings);
        fwrite(out,sdslen(out),1,stdout);
        fflush(stdout);
        sdsfree(out);
    }"""

new = """    if (output) {
        /* Proto mode GET: replace standard output */
        if (config.proto_mode && config.proto_get_key
            && reply->type == REDIS_REPLY_STRING) {
            cli_proto_format_get(config.proto_get_key, reply);
            sdsfree(config.proto_get_key);
            config.proto_get_key = NULL;
        } else if (config.proto_mode && config.proto_keys_mode
                   && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t ei = 0; ei < reply->elements; ei++) {
                printf("%zu) ", ei + 1);
                cli_proto_format_key_reply(reply->element[ei]);
                printf("\\n");
            }
            config.proto_keys_mode = 0;
        } else if (!(config.proto_mode && config.monitor_mode)) {
            out = cliFormatReply(reply, config.output, output_raw_strings);
            fwrite(out,sdslen(out),1,stdout);
            sdsfree(out);
        }

        fflush(stdout);
    }

    /* Proto mode MONITOR: hide raw line, show compact proto instead */
    if (config.proto_mode && config.monitor_mode
        && reply->type == REDIS_REPLY_STATUS)
    {
        cli_proto_format_monitor_line(reply->str);
    }"""

text = text.replace(old, new)

# 4. Add GET key tracking + keys mode + proto key arg translation
text = text.replace(
    'if (!strcasecmp(command,"monitor")) config.monitor_mode = 1;\n    int is_subscribe',
    'if (!strcasecmp(command,"monitor")) { config.monitor_mode = 1; if (argc >= 2) { cli_proto_monitor_set_filter(argv[1]); sdsfree(argv[1]); argv[1] = NULL; argc = 1; } else cli_proto_monitor_set_filter(NULL); }\n    int is_subscribe',
)

# 5. Add --proto option
text = text.replace(
    '} else if (!strcmp(argv[i],"--csv")) {\n            config.output = OUTPUT_CSV;',
    '} else if (!strcmp(argv[i],"--csv")) {\n            config.output = OUTPUT_CSV;\n        } else if (!strcmp(argv[i],"--proto")) {\n            config.proto_mode = 1;'
)

with open(sys.argv[1], 'w') as f:
    f.write(text)

print("patch applied")
