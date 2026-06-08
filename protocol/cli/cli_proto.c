/*
 * cli_proto.c — protobuf 解析引擎（字段描述符反射，零硬编码）
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <protobuf-c/protobuf-c.h>
#include <hiredis/hiredis.h>
#include <sds.h>
#include <sdscompat.h>

/* ════════════════════════════════════════════════════════
 * 注册表
 * ════════════════════════════════════════════════════════ */

typedef struct {
    const char                       *prefix;
    const ProtobufCMessageDescriptor *key_desc;
    const ProtobufCMessageDescriptor *entry_desc;
} proto_reg_t;

/* 由 gen_proto_registry.sh 自动生成的注册表 */
#include "proto_registry.c"

/* ════════════════════════════════════════════════════════
 * 通用反射打印机
 * ════════════════════════════════════════════════════════ */

static void print_message(const ProtobufCMessage *msg, int indent);

static void print_indent(int n)
{
    for (int i = 0; i < n; i++) printf("  ");
}

static const char *enum_name(const ProtobufCFieldDescriptor *fd, int32_t val)
{
    const ProtobufCEnumDescriptor *ed = (const ProtobufCEnumDescriptor *)fd->descriptor;
    const ProtobufCEnumValue *ev = protobuf_c_enum_descriptor_get_value(ed, val);
    return ev ? ev->name : "?";
}

static void print_field_value(const ProtobufCMessage *msg,
                               const ProtobufCFieldDescriptor *fd,
                               int indent)
{
    const uint8_t *base = (const uint8_t *)msg;
    const void    *ptr  = base + fd->offset;

    if (fd->label == PROTOBUF_C_LABEL_OPTIONAL) {
        const protobuf_c_boolean *has = (const protobuf_c_boolean *)(base + fd->quantifier_offset);
        if (!*has) { printf("-"); return; }
    }

    switch (fd->type) {
    case PROTOBUF_C_TYPE_INT32:    printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_SINT32:   printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_SFIXED32: printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_INT64:    printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_SINT64:   printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_SFIXED64: printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_UINT32:   printf("%u", *(const uint32_t *)ptr); break;
    case PROTOBUF_C_TYPE_FIXED32:  printf("%u", *(const uint32_t *)ptr); break;
    case PROTOBUF_C_TYPE_UINT64:   printf("%llu", (unsigned long long)*(const uint64_t *)ptr); break;
    case PROTOBUF_C_TYPE_FIXED64:  printf("%llu", (unsigned long long)*(const uint64_t *)ptr); break;
    case PROTOBUF_C_TYPE_FLOAT:    printf("%g", *(const float *)ptr); break;
    case PROTOBUF_C_TYPE_DOUBLE:   printf("%g", *(const double *)ptr); break;
    case PROTOBUF_C_TYPE_BOOL:     printf("%s", *(const protobuf_c_boolean *)ptr ? "true" : "false"); break;
    case PROTOBUF_C_TYPE_ENUM:     printf("%s(%d)", enum_name(fd, *(const int32_t *)ptr), *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_STRING:
        printf("%s", *(const char *const *)ptr ?: "");
        break;
    case PROTOBUF_C_TYPE_BYTES: {
        const ProtobufCBinaryData *bd = (const ProtobufCBinaryData *)ptr;
        if (bd->data && bd->len > 0)
            fwrite(bd->data, 1, bd->len, stdout);
        break;
    }
    case PROTOBUF_C_TYPE_MESSAGE: {
        const ProtobufCMessage *sub = *(const ProtobufCMessage *const *)ptr;
        if (sub) {
            printf("{\n");
            print_message(sub, indent + 1);
            print_indent(indent);
            printf("}");
        } else {
            printf("{}");
        }
        break;
    }
    default: printf("<unhandled type %d>", fd->type); break;
    }
}

static void print_message(const ProtobufCMessage *msg, int indent)
{
    if (!msg || !msg->descriptor) return;
    const ProtobufCMessageDescriptor *desc = msg->descriptor;

    for (unsigned i = 0; i < desc->n_fields; i++) {
        const ProtobufCFieldDescriptor *fd = &desc->fields[i];

        /* skip redundant index sub-message (key already shown) */
        if (fd->type == PROTOBUF_C_TYPE_MESSAGE && strcmp(fd->name, "index") == 0)
            continue;

        if (fd->label == PROTOBUF_C_LABEL_REPEATED) {
            const uint8_t *base = (const uint8_t *)msg;
            size_t count = *(const size_t *)(base + fd->quantifier_offset);
            if (count == 0 && indent == 0) continue;

            print_indent(indent);
            printf("%s=[%zu]", fd->name, count);
            if (count > 0) {
                printf("\n");
                void *const *elem_ptrs = *(void *const *const *)(base + fd->offset);
                for (size_t j = 0; j < count; j++) {
                    print_indent(indent + 1);
                    printf("[%zu] ", j);
                    if (fd->type == PROTOBUF_C_TYPE_MESSAGE && elem_ptrs && elem_ptrs[j]) {
                        const ProtobufCMessage *sub = (const ProtobufCMessage *)elem_ptrs[j];
                        printf("{\n");
                        print_message(sub, indent + 2);
                        print_indent(indent + 1);
                        printf("}");
                    } else {
                        printf("-");
                    }
                    printf("\n");
                }
            } else {
                printf("\n");
            }
        } else {
            print_indent(indent);
            printf("%s=", fd->name);
            print_field_value(msg, fd, indent);
            printf("\n");
        }
    }
}

/* ════════════════════════════════════════════════════════
 * API
 * ════════════════════════════════════════════════════════ */

void cli_proto_init(void) {}

static void try_print_proto(const char *key, size_t key_len,
                             const uint8_t *data, size_t dlen);
static uint8_t *encode_proto_key(const char *human_key, size_t *out_len);

static void try_print_proto(const char *key, size_t key_len,
                             const uint8_t *data, size_t dlen)
{
    for (int i = 0; i < g_proto_reg_count; i++) {
        const proto_reg_t *r = &g_proto_reg[i];
        size_t plen = strlen(r->prefix);
        if (key_len < plen || memcmp(key, r->prefix, plen) != 0)
            continue;

        const uint8_t *remaining = (const uint8_t *)key + plen;
        size_t rlen = key_len - plen;

        ProtobufCMessage *kmsg = protobuf_c_message_unpack(
            r->key_desc, NULL, rlen, remaining);
        if (!kmsg) continue;

        ProtobufCMessage *emsg = protobuf_c_message_unpack(
            r->entry_desc, NULL, dlen, data);
        if (emsg) {
            print_message(emsg, 0);
            protobuf_c_message_free_unpacked(emsg, NULL);
        } else {
            /* entry unpack 失败: 输出原始值 */
            printf("(raw) %.*s\n", (int)dlen, (const char *)data);
        }

        protobuf_c_message_free_unpacked(kmsg, NULL);
        return;
    }

    /* 未匹配: 原始输出 */
    printf("%.*s\n", (int)dlen, (const char *)data);
}

void cli_proto_format_get(const char *human_key, const redisReply *reply)
{
    if (!reply || reply->type != REDIS_REPLY_STRING) return;

    /* Encode to binary for proto matching */
    size_t blob_len = 0;
    uint8_t *blob = encode_proto_key(human_key, &blob_len);
    if (blob) {
        try_print_proto((const char *)blob, blob_len,
                        (const uint8_t *)reply->str, reply->len);
        free(blob);
    }
}

/* ════════════════════════════════════════════════════════
 * KEYS key parser: compact proto path for individual keys
 * ════════════════════════════════════════════════════════ */

static void print_message_compact(const ProtobufCMessage *msg);

void cli_proto_format_key_reply(const redisReply *reply)
{
    if (!reply || reply->type != REDIS_REPLY_STRING || !reply->str)
        return;

    const uint8_t *data = (const uint8_t *)reply->str;
    size_t dlen = reply->len;

    for (int i = 0; i < g_proto_reg_count; i++) {
        const proto_reg_t *r = &g_proto_reg[i];
        size_t preflen = strlen(r->prefix);
        if (dlen < preflen || memcmp(data, r->prefix, preflen) != 0)
            continue;

        printf("\"%s", r->prefix);

        size_t keydlen = dlen - preflen;
        ProtobufCMessage *kmsg = protobuf_c_message_unpack(
            r->key_desc, NULL, keydlen, data + preflen);
        if (kmsg) {
            print_message_compact(kmsg);
            protobuf_c_message_free_unpacked(kmsg, NULL);
        }
        printf("\"");
        return;
    }

    /* no match: print as-is */
    printf("%s", reply->str);
}

/* ════════════════════════════════════════════════════════
 * Proto key encoder: human → binary (for SET/GET/KEYS input)
 * ════════════════════════════════════════════════════════ */

static int set_field_by_name(ProtobufCMessage *msg,
                              const ProtobufCMessageDescriptor *desc,
                              const char *name, const char *value_str)
{
    for (unsigned i = 0; i < desc->n_fields; i++) {
        const ProtobufCFieldDescriptor *fd = &desc->fields[i];
        if (strcmp(fd->name, name) != 0) continue;

        uint8_t *base = (uint8_t *)msg;
        void *ptr = base + fd->offset;

        switch (fd->type) {
        case PROTOBUF_C_TYPE_UINT32:
        case PROTOBUF_C_TYPE_FIXED32:
            *(uint32_t *)ptr = (uint32_t)strtoul(value_str, NULL, 0);
            break;
        case PROTOBUF_C_TYPE_INT32:
        case PROTOBUF_C_TYPE_SINT32:
        case PROTOBUF_C_TYPE_SFIXED32:
            *(int32_t *)ptr = (int32_t)strtol(value_str, NULL, 0);
            break;
        case PROTOBUF_C_TYPE_UINT64:
        case PROTOBUF_C_TYPE_FIXED64:
            *(uint64_t *)ptr = strtoull(value_str, NULL, 0);
            break;
        case PROTOBUF_C_TYPE_INT64:
        case PROTOBUF_C_TYPE_SINT64:
        case PROTOBUF_C_TYPE_SFIXED64:
            *(int64_t *)ptr = strtoll(value_str, NULL, 0);
            break;
        case PROTOBUF_C_TYPE_BOOL:
            *(protobuf_c_boolean *)ptr =
                (strcasecmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
            break;
        case PROTOBUF_C_TYPE_STRING:
            /* strip surrounding double-quotes if present (MONITOR copy-paste compat) */
            {
                size_t slen = strlen(value_str);
                if (slen >= 2 && value_str[0] == '"' && value_str[slen - 1] == '"')
                    *(char **)ptr = strndup(value_str + 1, slen - 2);
                else
                    *(char **)ptr = strdup(value_str);
            }
            break;
        case PROTOBUF_C_TYPE_ENUM: {
            const ProtobufCEnumDescriptor *ed =
                (const ProtobufCEnumDescriptor *)fd->descriptor;
            const ProtobufCEnumValue *ev =
                protobuf_c_enum_descriptor_get_value_by_name(ed, value_str);
            if (!ev)
                ev = protobuf_c_enum_descriptor_get_value(
                    ed, (int)strtol(value_str, NULL, 0));
            if (!ev) return -1;
            *(int32_t *)ptr = ev->value;
            break;
        }
        default: return -1;
        }

        /* always set has flag: explicit fields always encode, even zero values */
        if (fd->label == PROTOBUF_C_LABEL_OPTIONAL)
            *(protobuf_c_boolean *)(base + fd->quantifier_offset) = 1;
        return 0;
    }
    return -1;
}

/* Encode human-readable proto key → raw binary, malloc'd, caller frees. */
static uint8_t *encode_proto_key(const char *human_key, size_t *out_len)
{
    for (int i = 0; i < g_proto_reg_count; i++) {
        const proto_reg_t *r = &g_proto_reg[i];
        size_t plen = strlen(r->prefix);
        if (strncmp(human_key, r->prefix, plen) != 0) continue;

        const char *fields_str = human_key + plen;
        while (*fields_str == ',' || *fields_str == ' ') fields_str++;

        if (*fields_str == '\0') {
            *out_len = plen;
            uint8_t *buf = malloc(plen + 1);
            memcpy(buf, r->prefix, plen);
            buf[plen] = '\0';
            return buf;
        }

        ProtobufCMessage *kmsg = calloc(1, r->key_desc->sizeof_message);
        if (!kmsg) return NULL;
        kmsg->descriptor = r->key_desc;

        char *dup = strdup(fields_str);
        char *save = NULL;
        char *tok = strtok_r(dup, ",", &save);
        int ok = 1;
        while (tok && ok) {
            while (*tok == ' ') tok++;
            char *eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                if (set_field_by_name(kmsg, r->key_desc, tok, eq + 1) != 0)
                    ok = 0;
            }
            tok = strtok_r(NULL, ",", &save);
        }
        free(dup);

        if (!ok) {
            free(kmsg);
            return NULL;
        }

        size_t klen = protobuf_c_message_get_packed_size(kmsg);
        *out_len = plen + klen;
        uint8_t *buf = malloc(*out_len);
        if (buf) {
            memcpy(buf, r->prefix, plen);
            if (klen > 0) protobuf_c_message_pack(kmsg, buf + plen);
        }

        /* free allocated strings */
        for (unsigned j = 0; j < r->key_desc->n_fields; j++) {
            const ProtobufCFieldDescriptor *fd = &r->key_desc->fields[j];
            if (fd->type == PROTOBUF_C_TYPE_STRING) {
                uint8_t *base = (uint8_t *)kmsg;
                free(*(char **)(base + fd->offset));
            }
        }
        free(kmsg);
        return buf;
    }

    *out_len = strlen(human_key);
    return (uint8_t *)strdup(human_key);
}

sds cli_proto_encode_key(const char *human_key)
{
    size_t len = 0;
    uint8_t *raw = encode_proto_key(human_key, &len);
    if (!raw) return sdsempty();
    sds result = sdsnewlen(raw, len);
    free(raw);
    return result;
}

/* Encode human-readable entry fields "f1=v1,f2=v2" into proto entry binary,
 * matching the entry descriptor from the key's prefix in the registry.
 * Returns sds (may be empty) on success, NULL if no registry match.
 * Note: entry.index is NOT populated here — middleware reconstructs it from the key. */
sds cli_proto_encode_entry_value(const char *human_key, const char *value_fields)
{
    for (int i = 0; i < g_proto_reg_count; i++) {
        const proto_reg_t *r = &g_proto_reg[i];
        size_t plen = strlen(r->prefix);
        if (strncmp(human_key, r->prefix, plen) != 0) continue;

        ProtobufCMessage *emsg = calloc(1, r->entry_desc->sizeof_message);
        if (!emsg) return NULL;
        emsg->descriptor = r->entry_desc;

        char *dup = strdup(value_fields);
        char *save = NULL;
        char *tok = strtok_r(dup, ",", &save);
        int ok = 1;
        while (tok && ok) {
            while (*tok == ' ') tok++;
            char *eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                if (set_field_by_name(emsg, r->entry_desc, tok, eq + 1) != 0)
                    ok = 0;
            }
            tok = strtok_r(NULL, ",", &save);
        }
        free(dup);

        if (!ok) {
            free(emsg);
            return NULL;
        }

        size_t mlen = protobuf_c_message_get_packed_size(emsg);
        sds result = sdsnewlen(NULL, mlen);
        protobuf_c_message_pack(emsg, result);

        /* free allocated strings */
        for (unsigned j = 0; j < r->entry_desc->n_fields; j++) {
            const ProtobufCFieldDescriptor *fd = &r->entry_desc->fields[j];
            if (fd->type == PROTOBUF_C_TYPE_STRING) {
                uint8_t *base = (uint8_t *)emsg;
                free(*(char **)(base + fd->offset));
            }
        }
        free(emsg);
        return result;
    }

    return NULL;
}

/* Translate glob pattern: replace proto paths with raw prefix, drop field filters.
-   e.g. "*s_pd_interface/sw=0*" becomes "*s_pd_interface*" */
sds cli_proto_translate_pattern(const char *pattern)
{
    sds out = sdsempty();

    for (int ri = 0; ri < g_proto_reg_count; ri++) {
        const char *pfx = g_proto_reg[ri].prefix;
        size_t plen = strlen(pfx);

        /* scan pattern for this prefix */
        const char *hit = pattern;
        while ((hit = strstr(hit, pfx)) != NULL) {
            /* copy everything before this hit */
            out = sdscatlen(out, pattern, hit - pattern);

            /* scan ahead to find end of proto path (up to wildcard or end) */
            const char *end = hit + plen;
            while (*end && *end != '*' && *end != '?' && *end != '[') end++;

            /* encode the proto path */
            char *path = malloc((size_t)(end - hit) + 1);
            memcpy(path, hit, (size_t)(end - hit));
            path[end - hit] = '\0';

            uint8_t *raw = encode_proto_key(path, &plen);
            if (raw) {
                out = sdscatlen(out, raw, plen);
                free(raw);
            }
            free(path);

            pattern = end;   /* continue scanning remainder */
            hit = pattern;
        }

        /* append trailing part */
        out = sdscat(out, pattern);
        /* if we made a replacement, we're done (one prefix at a time) */
        if (sdslen(out) > 0) return out;
        /* reset and try next prefix */
        sdsclear(out);
        out = sdscat(out, ""); /* keep non-empty for next iteration */
    }

    /* no match: return copy */
    return sdsnew(pattern);
}

/* ════════════════════════════════════════════════════════
 * Compact single-line print (for MONITOR)
 * ════════════════════════════════════════════════════════ */

static void print_message_compact(const ProtobufCMessage *msg);

static void print_field_value_compact(const ProtobufCMessage *msg,
                                       const ProtobufCFieldDescriptor *fd)
{
    const uint8_t *base = (const uint8_t *)msg;
    const void    *ptr  = base + fd->offset;
    /* compact: always print, no has check */

    switch (fd->type) {
    case PROTOBUF_C_TYPE_INT32:    printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_SINT32:   printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_SFIXED32: printf("%d", *(const int32_t *)ptr); break;
    case PROTOBUF_C_TYPE_INT64:    printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_SINT64:   printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_SFIXED64: printf("%lld", (long long)*(const int64_t *)ptr); break;
    case PROTOBUF_C_TYPE_UINT32:   printf("%u", *(const uint32_t *)ptr); break;
    case PROTOBUF_C_TYPE_FIXED32:  printf("%u", *(const uint32_t *)ptr); break;
    case PROTOBUF_C_TYPE_UINT64:   printf("%llu", (unsigned long long)*(const uint64_t *)ptr); break;
    case PROTOBUF_C_TYPE_FIXED64:  printf("%llu", (unsigned long long)*(const uint64_t *)ptr); break;
    case PROTOBUF_C_TYPE_FLOAT:    printf("%g", *(const float *)ptr); break;
    case PROTOBUF_C_TYPE_DOUBLE:   printf("%g", *(const double *)ptr); break;
    case PROTOBUF_C_TYPE_BOOL:     printf("%s", *(const protobuf_c_boolean *)ptr ? "true" : "false"); break;
    case PROTOBUF_C_TYPE_ENUM:     printf("%s", enum_name(fd, *(const int32_t *)ptr)); break;
    case PROTOBUF_C_TYPE_STRING:
        printf("%s", *(const char *const *)ptr ?: "");
        break;
    case PROTOBUF_C_TYPE_BYTES: printf("..."); break;
    case PROTOBUF_C_TYPE_MESSAGE: {
        const ProtobufCMessage *sub = *(const ProtobufCMessage *const *)ptr;
        if (sub) { printf("{"); print_message_compact(sub); printf("}"); }
        else printf("{}");
        break;
    }
    default: printf("?"); break;
    }
}

static void print_message_compact(const ProtobufCMessage *msg)
{
    if (!msg || !msg->descriptor) return;
    const ProtobufCMessageDescriptor *desc = msg->descriptor;
    int first = 1;

    for (unsigned i = 0; i < desc->n_fields; i++) {
        const ProtobufCFieldDescriptor *fd = &desc->fields[i];

        /* skip redundant index sub-message (key already shown) */
        if (fd->type == PROTOBUF_C_TYPE_MESSAGE && strcmp(fd->name, "index") == 0)
            continue;

        if (fd->label == PROTOBUF_C_LABEL_REPEATED) {
            const uint8_t *base = (const uint8_t *)msg;
            size_t count = *(const size_t *)(base + fd->quantifier_offset);
            if (!first) printf(",");
            printf("%s=%zu", fd->name, count);
            first = 0;
        } else {
            if (!first) printf(",");
            printf("%s=", fd->name);
            print_field_value_compact(msg, fd);
            first = 0;
        }
    }
}

/* Parse protobuf key, output "prefix/f1=v1,f2=v2". payload handled separately. */
static void try_print_proto_compact(const uint8_t *raw_key, size_t klen,
                                     const uint8_t *raw_payload, size_t plen)
{
    for (int i = 0; i < g_proto_reg_count; i++) {
        const proto_reg_t *r = &g_proto_reg[i];
        size_t preflen = strlen(r->prefix);
        if (klen < preflen || memcmp(raw_key, r->prefix, preflen) != 0)
            continue;

        /* Print prefix */
        printf("\"%s", r->prefix);

        /* Parse key protobuf suffix (even empty → shows defaults) */
        size_t keydlen = klen - preflen;
        ProtobufCMessage *kmsg = protobuf_c_message_unpack(
            r->key_desc, NULL, keydlen, raw_key + preflen);
        if (kmsg) {
            print_message_compact(kmsg);
            protobuf_c_message_free_unpacked(kmsg, NULL);
        }

        printf("\" ");

        /* Parse payload compact */
        ProtobufCMessage *emsg = protobuf_c_message_unpack(
            r->entry_desc, NULL, plen, raw_payload);
        if (emsg) {
            printf("\"");
            print_message_compact(emsg);
            printf("\"");
            protobuf_c_message_free_unpacked(emsg, NULL);
        } else {
            printf("?");
        }
        printf("\n");
        return;
    }

    /* No match: hex */
    printf("%.*s ", (int)klen, raw_key);
    for (size_t j = 0; j < plen && j < 64; j++) printf("%02x", raw_payload[j]);
    if (plen > 64) printf("...");
    printf("\n");
}

/* ════════════════════════════════════════════════════════
 * MONITOR line parser: compact proto display, no raw line
 * ════════════════════════════════════════════════════════ */

static uint8_t *unescape_c_string(const char *s, size_t *out_len)
{
    size_t len = strlen(s);
    uint8_t *buf = malloc(len + 1);
    if (!buf) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) {
            i++;
            switch (s[i]) {
            case 'n': buf[j++] = '\n'; break;
            case 't': buf[j++] = '\t'; break;
            case 'r': buf[j++] = '\r'; break;
            case 'b': buf[j++] = '\b'; break;
            case 'f': buf[j++] = '\f'; break;
            case '\\': buf[j++] = '\\'; break;
            case '"': buf[j++] = '"'; break;
            case '0': buf[j++] = '\0'; break;
            case 'x':
                if (i + 2 < len) {
                    char hex[3] = {s[i+1], s[i+2], '\0'};
                    buf[j++] = (uint8_t)strtol(hex, NULL, 16);
                    i += 2;
                } else buf[j++] = '\\';
                break;
            default: buf[j++] = s[i]; break;
            }
        } else {
            buf[j++] = s[i];
        }
    }
    *out_len = j;
    return buf;
}

void cli_proto_format_monitor_line(const char *line)
{
    if (!line) return;

    /* Extract prefix "TIMESTAMP [DB CLIENT]" */
    const char *bend = strchr(line, ']');
    if (!bend) return;
    size_t plen = (size_t)(bend - line) + 1;

    /* Skip "OK" sentinel */
    if (plen <= 3 && strncmp(line, "OK", 2) == 0) return;

    /* Print timestamp + bracket */
    fwrite(line, 1, plen, stdout);
    printf(" ");

    /* Parse quoted args after bracket */
    const char *p = bend + 1;
    while (*p == ' ') p++;

    char **args = malloc(16 * sizeof(char *));
    int na = 0, ca = 16;
    if (!args) return;

    while (*p) {
        if (*p != '"') { p++; continue; }
        p++;
        const char *start = p;
        while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
        size_t alen = (size_t)(p - start);
        char *arg = malloc(alen + 1);
        if (!arg) break;
        memcpy(arg, start, alen); arg[alen] = '\0';
        if (na >= ca) { ca *= 2; args = realloc(args, (size_t)ca * sizeof(char *)); }
        args[na++] = arg;
        if (*p == '"') p++;
    }

    if (na < 3) goto cleanup;

    const char *cmd = args[0];
    const char *key = NULL;
    const char *payload = NULL;

    if (!strcasecmp(cmd, "set")) {
        key = args[1]; payload = args[2];
    } else if (!strcasecmp(cmd, "publish")) {
        key = strrchr(args[1], ':');
        key = key ? key + 1 : args[1];
        payload = args[2];
    }

    if (key && payload) {
        printf("\"%s\" ", cmd);
        size_t klen = 0, plen = 0;
        uint8_t *rk = unescape_c_string(key, &klen);
        uint8_t *rp = unescape_c_string(payload, &plen);
        if (rk && rp && plen > 0)
            try_print_proto_compact(rk, klen, rp, plen);
        free(rk); free(rp);
    }

cleanup:
    for (int i = 0; i < na; i++) free(args[i]);
    free(args);
}
