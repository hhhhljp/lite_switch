#include "PD/interface/interface.pb-c.h"
#include "middleware.h"
#include <zlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ═══════════════════════════════════════════════════════════════════
   switch-web — 交换机端口状态 Web 控制台
   HTTP + SSE + Redis Pub/Sub 单线程 select 多路复用
   ═══════════════════════════════════════════════════════════════════ */

#define PORT        8080
#define BACKLOG     16
#define MAX_CLIENTS 32
#define MAX_SSE     16
#define MAX_IFACES  256

/* ── Proto 静态模板 ── */
static PdInterface__PdInterfaceKey   g_pd_key   = PD_INTERFACE__PD_INTERFACE_KEY__INIT;
static PdInterface__PdInterfaceEntry g_pd_entry = PD_INTERFACE__PD_INTERFACE_ENTRY__INIT;

/* ── 接口缓存 ── */
static PdInterface__PdInterfaceEntry *g_cache[MAX_IFACES];
static int                            g_cache_count = 0;

/* ── SSE 客户端 fd 列表 ── */
static int g_sse_fds[MAX_SSE];
static int g_sse_count = 0;

/* ── mw 上下文 ── */
static mw_context_t *g_ctx;

/* ═══════════════════════════════════════════════════════════════════
   接口缓存操作
   ═══════════════════════════════════════════════════════════════════ */

static int cache_find(uint32_t sw, uint32_t port)
{
    for (int i = 0; i < g_cache_count; i++) {
        PdInterface__PdInterfaceEntry *e = g_cache[i];
        if (e && e->index && e->index->sw == sw && e->index->port == port)
            return i;
    }
    return -1;
}

static void cache_upsert(PdInterface__PdInterfaceEntry *entry)
{
    if (!entry || !entry->index) return;
    int idx = cache_find(entry->index->sw, entry->index->port);
    if (idx >= 0) {
        mw_free_message((ProtobufCMessage *)g_cache[idx]);
        g_cache[idx] = entry;
    } else if (g_cache_count < MAX_IFACES) {
        g_cache[g_cache_count++] = entry;
    } else {
        mw_free_message((ProtobufCMessage *)entry);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   JSON / SSE 序列化
   ═══════════════════════════════════════════════════════════════════ */

static const char *speed_str(PdInterface__PdInterfaceSpeed s)
{
    switch (s) {
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_10M:   return "10M";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_100M:  return "100M";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_1G:    return "1G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_2500M: return "2.5G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_5G:    return "5G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_10G:   return "10G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_20G:   return "20G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_25G:   return "25G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_40G:   return "40G";
    case PD_INTERFACE__PD_INTERFACE_SPEED__SPEED_100G:  return "100G";
    default: return "?";
    }
}

static const char *admin_str(PdInterface__PdInterfaceAdminMode m)
{
    return m == PD_INTERFACE__PD_INTERFACE_ADMIN_MODE__ADMIN_UP ? "UP" : "DOWN";
}

static const char *link_str(PdInterface__PdInterfaceLinkState s)
{
    switch (s) {
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP:           return "UP";
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN:         return "DOWN";
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_LOCAL_FAULT:  return "FAULT";
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_REMOTE_FAULT: return "RMT_FAULT";
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_PARTIALLY_UP: return "PARTIAL";
    case PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DFE_TUNING:   return "TUNING";
    default: return "?";
    }
}

static const char *link_css(PdInterface__PdInterfaceLinkState s)
{
    if (s == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_UP)
        return "status-up";
    if (s == PD_INTERFACE__PD_INTERFACE_LINK_STATE__LINK_DOWN)
        return "status-down";
    return "status-warn";
}

/* 将一条接口序列化为 JSON 一行 */
static int entry_to_json(char *buf, size_t cap, PdInterface__PdInterfaceEntry *e)
{
    if (!e || !e->index) return 0;
    return snprintf(buf, cap,
        "{\"sw\":%u,\"port\":%u,\"name\":\"%s\",\"speed\":\"%s\","
        "\"admin\":\"%s\",\"link\":\"%s\",\"css\":\"%s\",\"mtu\":%u,"
        "\"mac\":\"%s\",\"lanes\":%u}",
        e->index->sw, e->index->port,
        e->port_name ? e->port_name : "",
        speed_str(e->speed),
        admin_str(e->admin_mode),
        link_str(e->link_state),
        link_css(e->link_state),
        e->mtu,
        e->mac_addr ? e->mac_addr : "",
        e->num_lanes);
}

/* ═══════════════════════════════════════════════════════════════════
   SSE 广播
   ═══════════════════════════════════════════════════════════════════ */

static void sse_add(int fd)
{
    if (g_sse_count >= MAX_SSE) { close(fd); return; }
    /* 设为非阻塞 */
    /* select() provides async */ int _unused =
    /* keep blocking for reliable write */
    g_sse_fds[g_sse_count++] = fd;
    dzlog_info("SSE client connected (fd=%d), total=%d", fd, g_sse_count);
}

static void sse_remove(int fd)
{
    for (int i = 0; i < g_sse_count; i++) {
        if (g_sse_fds[i] == fd) {
            g_sse_fds[i] = g_sse_fds[--g_sse_count];
            close(fd);
            dzlog_info("SSE client disconnected (fd=%d), total=%d", fd, g_sse_count);
            return;
        }
    }
}

static void sse_broadcast(const char *json)
{
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "event: interface\ndata: %s\n\n", json);
    for (int i = g_sse_count - 1; i >= 0; i--) {
        ssize_t w = write(g_sse_fds[i], buf, (size_t)n);
        if (w <= 0) {
            sse_remove(g_sse_fds[i]);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   回调 — 订阅 & scan 共用
   ═══════════════════════════════════════════════════════════════════ */

static void on_interface(const char *event, void *value, void *user_data)
{
    (void)user_data;

    if (strcmp(event, "set") == 0) {
        PdInterface__PdInterfaceEntry *entry = (PdInterface__PdInterfaceEntry *)value;
        if (entry && entry->index) {
            cache_upsert(entry);
            char json[512];
            entry_to_json(json, sizeof(json), entry);
            sse_broadcast(json);
            dzlog_info("interface sw=%u port=%u link=%s",
                       entry->index->sw, entry->index->port,
                       link_str(entry->link_state));
        } else {
            if (entry) mw_free_message((ProtobufCMessage *)entry);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   HTTP 辅助
   ═══════════════════════════════════════════════════════════════════ */

static void http_ok(int fd, const char *content_type)
{
    dprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n", content_type);
}

static void http_json(int fd, const char *json)
{
    http_ok(fd, "application/json");
    dprintf(fd, "%s", json);
}

/* ═══════════════════════════════════════════════════════════════════
   GET /api/v1/interfaces — 全量 JSON
   ═══════════════════════════════════════════════════════════════════ */

static void handle_interfaces(int fd)
{
    char buf[32768];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "{\"interfaces\":[");

    for (int i = 0; i < g_cache_count; i++) {
        if (i > 0) buf[off++] = ',';
        char line[512];
        int n = entry_to_json(line, sizeof(line), g_cache[i]);
        memcpy(buf + off, line, (size_t)n);
        off += n;
    }

    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]}");
    http_json(fd, buf);
}

/* ═══════════════════════════════════════════════════════════════════
   GET /api/v1/events — SSE 流
   ═══════════════════════════════════════════════════════════════════ */

static void handle_events(int fd)
{
    dprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n");
    /* 先发送当前全量 */
    for (int i = 0; i < g_cache_count; i++) {
        char json[512];
        entry_to_json(json, sizeof(json), g_cache[i]);
        dprintf(fd, "event: interface\ndata: %s\n\n", json);
    }
    sse_add(fd);
}

/* ═══════════════════════════════════════════════════════════════════
   GET / — HTML 页面
   ═══════════════════════════════════════════════════════════════════ */

static const char *HTML =
"<!DOCTYPE html>"
"<html lang=\"zh\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>lite_switch — Port Status</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#f0f2f5;color:#1a1a2e;min-height:100vh}"
".header{background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;padding:20px 32px}"
".header h1{font-size:22px;font-weight:600;letter-spacing:-.5px}"
".header span{font-size:13px;opacity:.6;margin-left:12px}"
".stats{display:flex;gap:16px;padding:24px 32px 0}"
".stat-card{flex:1;background:#fff;border-radius:12px;padding:18px 20px;box-shadow:0 1px 3px rgba(0,0,0,.06)}"
".stat-card .num{font-size:28px;font-weight:700}"
".stat-card .lbl{font-size:12px;color:#8b8fa3;margin-top:4px;text-transform:uppercase;letter-spacing:.5px}"
".stat-total .num{color:#1a1a2e}"
".stat-up .num{color:#10b981}"
".stat-down .num{color:#ef4444}"
".main{padding:24px 32px}"
"table{width:100%;background:#fff;border-radius:12px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,.06);border-collapse:collapse}"
"thead{background:#f8f9fb}"
"th{text-align:left;padding:12px 16px;font-size:11px;font-weight:600;text-transform:uppercase;color:#8b8fa3;letter-spacing:.5px;border-bottom:1px solid #eef0f4}"
"td{padding:12px 16px;font-size:13px;border-bottom:1px solid #f0f2f5}"
"tr:last-child td{border-bottom:none}"
"tr:hover td{background:#f8f9fb;transition:background .15s}"
".badge{display:inline-block;padding:3px 10px;border-radius:10px;font-size:11px;font-weight:600;letter-spacing:.3px}"
".status-up{background:#d1fae5;color:#065f46}"
".status-down{background:#fee2e2;color:#991b1b}"
".status-warn{background:#fef3c7;color:#92400e}"
".mac{font-family:'SF Mono',Monaco,monospace;font-size:12px;color:#6b7280}"
".port-id{font-family:'SF Mono',Monaco,monospace;font-weight:600;color:#3b82f6}"
"@keyframes flash{from{background:#dbeafe}to{background:transparent}}"
".flash{animation:flash .6s ease-out}"
".empty{text-align:center;padding:48px;color:#9ca3af;font-size:14px}"
"@media(max-width:768px){.stats{flex-direction:column}}"
"</style>"
"</head>"
"<body>"
"<div class=\"header\">"
"<h1>⚡ lite_switch<span>Port Status Monitor</span></h1>"
"</div>"
"<div class=\"stats\">"
"<div class=\"stat-card stat-total\"><div class=\"num\" id=\"stat-total\">0</div><div class=\"lbl\">Total Ports</div></div>"
"<div class=\"stat-card stat-up\"><div class=\"num\" id=\"stat-up\">0</div><div class=\"lbl\">Link Up</div></div>"
"<div class=\"stat-card stat-down\"><div class=\"num\" id=\"stat-down\">0</div><div class=\"lbl\">Link Down</div></div>"
"</div>"
"<div class=\"main\">"
"<table>"
"<thead><tr><th>Port</th><th>Name</th><th>Speed</th><th>Admin</th><th>Link</th><th>MTU</th><th>MAC</th><th>Lanes</th></tr></thead>"
"<tbody id=\"tbody\"><tr><td colspan=\"8\" class=\"empty\">Waiting for data…</td></tr></tbody>"
"</table>"
"</div>"
"<script>"
"const tbody=document.getElementById('tbody');"
"const st=document.getElementById('stat-total');"
"const su=document.getElementById('stat-up');"
"const sd=document.getElementById('stat-down');"
"const rows=new Map();"
"let first=1;"
"function render(){"
"let total=rows.size,up=0,down=0;"
"rows.forEach(r=>{if(r.link==='UP')up++;else down++});"
"st.textContent=total;su.textContent=up;sd.textContent=down;"
"let h='';"
"rows.forEach(r=>{"
"h+='<tr'+(r.flash?' class=\"flash\"':'')+'>';"
"h+='<td><span class=\"port-id\">'+r.sw+':'+r.port+'</span></td>';"
"h+='<td>'+r.name+'</td><td>'+r.speed+'</td>';"
"h+='<td><span class=\"badge '+(r.admin==='UP'?'status-up':'status-down')+'\">'+r.admin+'</span></td>';"
"h+='<td><span class=\"badge '+r.css+'\">'+r.link+'</span></td>';"
"h+='<td>'+r.mtu+'</td><td class=\"mac\">'+r.mac+'</td><td>'+r.lanes+'</td></tr>';"
"});"
"if(!rows.size)h='<tr><td colspan=\"8\" class=\"empty\">No interfaces found</td></tr>';"
"tbody.innerHTML=h;"
"if(!first)setTimeout(()=>{rows.forEach(r=>r.flash=0);render()},600);"
"first=0;"
"}"
"const es=new EventSource('/api/v1/events');"
"es.addEventListener('interface',e=>{"
"const d=JSON.parse(e.data);"
"const k=d.sw+':'+d.port;"
"d.flash=rows.has(k);"
"rows.set(k,d);"
"render();"
"});"
"es.onerror=()=>{console.log('SSE reconnecting...');};"
"</script>"
"</body></html>";

static void handle_root(int fd)
{
    http_ok(fd, "text/html; charset=utf-8");
    write(fd, HTML, strlen(HTML));
}

/* ═══════════════════════════════════════════════════════════════════
   HTTP 请求解析 & 路由
   ═══════════════════════════════════════════════════════════════════ */

static void handle_http(int fd)
{
    char buf[2048];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { if (n < 0) dzlog_warn("read fd=%d err=%s", fd, strerror(errno)); close(fd); return; }
    buf[n] = '\0';

    /* 解析请求行 */
    char method[16], path[256];
    if (sscanf(buf, "%15s %255s", method, path) != 2) {
        close(fd);
        return;
    }
    dzlog_info("req %s %s", method, path);

    if (strcmp(method, "GET") != 0) {
        dprintf(fd, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        close(fd);
        return;
    }

    if (strcmp(path, "/") == 0) {
        handle_root(fd);
    } else if (strcmp(path, "/api/v1/interfaces") == 0) {
        handle_interfaces(fd);
    } else if (strcmp(path, "/api/v1/events") == 0) {
        handle_events(fd);
        return; /* SSE 保持连接 */
    } else {
        dprintf(fd, "HTTP/1.1 404 Not Found\r\n\r\n");
    }
    close(fd);
}

/* ═══════════════════════════════════════════════════════════════════
   main
   ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
    if (mw_log_init("switch-web") != 0) {
        fprintf(stderr, "zlog init failed\n");
        return 1;
    }
    dzlog_info("switch-web starting");

    /* ── Redis 建联 ── */
    g_ctx = mw_connect("127.0.0.1", 6379);
    if (!g_ctx) {
        dzlog_error("Redis connect failed");
        return 1;
    }

    /* ── 注册回调 ── */
    mw_callback_entry callbacks[] = {
        { .key_msg = (const ProtobufCMessage *)&g_pd_key,
          .entry_msg = (const ProtobufCMessage *)&g_pd_entry,
          .cb = on_interface },
    };
    mw_subscribe_keys(g_ctx, callbacks, 1);

    /* ── 扫描存量 ── */
    mw_scan_entry scan_entries[] = {
        { .key_msg = (const ProtobufCMessage *)&g_pd_key,
          .entry_msg = (const ProtobufCMessage *)&g_pd_entry },
    };
    int scanned = mw_scan(g_ctx, scan_entries, 1);
    dzlog_info("mw_scan enqueued %d event(s)", scanned);

    /* ── 消费 scan 队列 ── */
    {
        int r = 0;
        while (r < scanned) { if (mw_poll(g_ctx, 0) > 0) r++; }
        dzlog_info("consumed %d scan events, cache=%d", r, g_cache_count);
    }

    /* ── 触发懒订阅（建立 sub_conn + PSUBSCRIBE）── */
    {
        int rc = mw_poll(g_ctx, 0);
        dzlog_info("lazy subscribe triggered, rc=%d, sub_fd=%d",
                   rc, mw_get_sub_fd(g_ctx));
    }

    /* ── HTTP socket ── */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { dzlog_error("socket failed"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(PORT),
                                .sin_addr.s_addr = INADDR_ANY };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        dzlog_error("bind :%d failed", PORT);
        return 1;
    }
    listen(listen_fd, BACKLOG);
    dzlog_info("HTTP listening on http://0.0.0.0:%d", PORT);

    /* ── 客户端连接池 ── */
    int clients[MAX_CLIENTS];
    int client_count = 0;

    /* ── 主循环 ── */
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i], &rfds);
            if (clients[i] > maxfd) maxfd = clients[i];
        }

        /* Redis Pub/Sub fd — select() 驱动，阻塞只在有数据时调用 */
        int redis_fd = mw_get_sub_fd(g_ctx);
        if (redis_fd >= 0) {
            FD_SET(redis_fd, &rfds);
            if (redis_fd > maxfd) maxfd = redis_fd;
        }

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0 && errno != EINTR) {
            dzlog_error("select error: %s", strerror(errno));
            break;
        }

        /* ── Redis Pub/Sub 事件 ── */
        if (redis_fd >= 0 && FD_ISSET(redis_fd, &rfds)) {
            mw_poll(g_ctx, 0);
        }

        /* ── HTTP accept ── */
        if (FD_ISSET(listen_fd, &rfds)) {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0) {
                if (client_count < MAX_CLIENTS) {
                    clients[client_count++] = cfd;
                } else {
                    close(cfd);
                }
            }
        }

        /* ── HTTP 请求处理 ── */
        for (int i = client_count - 1; i >= 0; i--) {
            int fd = clients[i];
            if (FD_ISSET(fd, &rfds)) {
                handle_http(fd);
                for (int j = i; j < client_count - 1; j++)
                    clients[j] = clients[j + 1];
                client_count--;
            }
        }
    }

    mw_disconnect(g_ctx);
    return 0;
}
