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
    'if (!strcasecmp(command,"monitor")) config.monitor_mode = 1;\n    if (config.proto_mode && (!strcasecmp(command,"keys") || !strcasecmp(command,"scan")))\n        config.proto_keys_mode = 1;\n\n    /* Track GET key for proto parsing */\n    if (config.proto_get_key) { sdsfree(config.proto_get_key); config.proto_get_key = NULL; }\n    if (config.proto_mode && !strcasecmp(command,"get") && argc >= 2)\n        config.proto_get_key = sdsnew(argv[1]);\n\n    /* Save original human-readable key for SET (used by value encoder) */\n    if (config.proto_set_key) { sdsfree(config.proto_set_key); config.proto_set_key = NULL; }\n    if (config.proto_mode && !strcasecmp(command,"set") && argc >= 2)\n        config.proto_set_key = sdsnew(argv[1]);\n\n    /* Translate proto key args from human → binary before sending */\n    if (config.proto_mode && argc >= 2) {\n        if (!strcasecmp(command,"keys") || !strcasecmp(command,"scan")) {\n            sds newpat = cli_proto_translate_pattern(argv[1]);\n            if (sdslen(newpat) > 0) { sdsfree(argv[1]); argv[1] = newpat; }\n            else { sdsfree(newpat); }\n        } else if (!strcasecmp(command,"get") || !strcasecmp(command,"set")\n                || !strcasecmp(command,"del") || !strcasecmp(command,"exists")\n                || !strcasecmp(command,"type") || !strcasecmp(command,"ttl")) {\n            sds enc = cli_proto_encode_key(argv[1]);\n            if (sdslen(enc) > 0) { sdsfree(argv[1]); argv[1] = enc; }\n            else { sdsfree(enc); }\n        }\n    }\n\n    /* Translate proto value from human → binary for SET */\n    if (config.proto_mode && !strcasecmp(command,"set") && argc >= 3) {\n        sds encv = cli_proto_encode_entry_value(config.proto_set_key, argv[2]);\n        if (encv) { sdsfree(argv[2]); argv[2] = encv; }\n    }\n\n    int is_subscribe'
)

# 5. Add --proto option
text = text.replace(
    '} else if (!strcmp(argv[i],"--csv")) {\n            config.output = OUTPUT_CSV;',
    '} else if (!strcmp(argv[i],"--csv")) {\n            config.output = OUTPUT_CSV;\n        } else if (!strcmp(argv[i],"--proto")) {\n            config.proto_mode = 1;'
)

with open(sys.argv[1], 'w') as f:
    f.write(text)

print("patch applied")
