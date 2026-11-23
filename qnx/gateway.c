/* Minimal HTTP/1.0 JSON API for the dashboard. Talks to the controller only through
 * /dev/pump, injects faults with real signals, and starts the CPU load generator. */
#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>

extern char **environ;
static struct shared *sh;
static const char *loadgen_path = "/proc/boot/loadgen";
static pid_t load_pid = -1;

static const char *ev_name(uint32_t k)
{
    static const char *n[] = { "", "boot", "start", "pause", "resume", "stop", "done", "resumed",
        "fault_kill", "fault_hang", "died", "hung", "restarted", "hw_trip", "deadline_miss", "load",
        "motor_lost" };
    return k < sizeof n / sizeof *n ? n[k] : "?";
}

static int pump_read(char *b, size_t n)
{
    int fd = open("/dev/pump", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, b, n - 1);
    close(fd);
    if (r <= 0) return -1;
    while (r && (b[r - 1] == '\n')) r--;
    b[r] = 0;
    return (int)r;
}

static int pump_write(const char *cmd)
{
    int fd = open("/dev/pump", O_WRONLY);
    if (fd < 0) return errno;
    ssize_t r = write(fd, cmd, strlen(cmd));
    int e = r < 0 ? errno : 0;
    close(fd);
    return e;
}

static size_t status(char *b, size_t n)
{
    char pump[1024];
    struct utsname u;
    uname(&u);
    uint64_t t = now_ns();
    if (sh->load_mode && t > sh->load_until_ns) sh->load_mode = 0;
    size_t len = (size_t)snprintf(b, n,
        "{\"now_ms\":%llu,\"os\":{\"sysname\":\"%s\",\"release\":\"%s\",\"machine\":\"%s\",\"cpus\":%ld},"
        "\"load\":{\"mode\":\"%s\",\"remaining_ms\":%llu},\"pump\":%s,\"events\":[",
        (unsigned long long)(t / 1000000), u.sysname, u.release, u.machine, sysconf(_SC_NPROCESSORS_ONLN),
        sh->load_mode == 1 ? "rogue" : sh->load_mode == 2 ? "budgeted" : "off",
        (unsigned long long)(sh->load_mode ? (sh->load_until_ns - t) / 1000000 : 0),
        pump_read(pump, sizeof pump) > 0 ? pump : "null");
    uint64_t head = sh->ev_head, from = head > 40 ? head - 40 : 0;
    for (uint64_t i = from; i < head && len < n - 160; i++) {
        struct event e = sh->ev[i % EV_RING];
        if (e.seq != i + 1) continue;
        len += (size_t)snprintf(b + len, n - len, "%s{\"id\":%llu,\"t_ms\":%llu,\"kind\":\"%s\",\"gen\":%u,\"a\":%lld,\"b\":%lld}",
            len && b[len - 1] == '[' ? "" : ",", (unsigned long long)e.seq,
            (unsigned long long)(e.t_ns / 1000000), ev_name(e.kind), e.gen, (long long)e.a, (long long)e.b);
    }
    len += (size_t)snprintf(b + len, n - len, "]}");
    return len < n ? len : n - 1;
}

static int fault(int sig, uint32_t kind)
{
    pid_t pid = sh->pumpd_pid;
    if (pid <= 0) return ESRCH;
    sh->fault_ns = now_ns();
    ev_push(sh, kind, pid, 0);
    return kill(pid, sig) ? errno : 0;
}

static int load(const char *mode)
{
    if (load_pid > 0) { kill(load_pid, SIGKILL); load_pid = -1; }
    sh->load_mode = 0;
    if (!strcmp(mode, "off")) { ev_push(sh, EV_LOAD, 0, 0); return 0; }
    int rogue = !strcmp(mode, "rogue");
    if (!rogue && strcmp(mode, "budgeted")) return EINVAL;
    const char *secs = rogue ? "3" : "15";
    char nthr[8];
    snprintf(nthr, sizeof nthr, "%ld", sysconf(_SC_NPROCESSORS_ONLN));
    char *argv[] = { (char *)loadgen_path, (char *)mode, (char *)secs, nthr, NULL };
    int e = posix_spawn(&load_pid, loadgen_path, NULL, NULL, argv, environ);
    if (e) { load_pid = -1; return e; }
    sh->load_until_ns = now_ns() + (uint64_t)atoi(secs) * 1000000000ULL;
    sh->load_mode = rogue ? 1 : 2;
    ev_push(sh, EV_LOAD, sh->load_mode, 0);
    return 0;
}

static const char *query(const char *q, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = q; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            size_t i = 0;
            for (p += kl + 1; *p && *p != '&' && i + 1 < n; p++)
                if ((*p >= '0' && *p <= '9') || *p == '.' || (*p >= 'a' && *p <= 'z')) out[i++] = *p;
                else return NULL;
            if (*p && *p != '&') return NULL;
            out[i] = 0;
            return out;
        }
    }
    return NULL;
}

static void reply(int fd, int code, const char *body)
{
    char h[256];
    int n = snprintf(h, sizeof h, "HTTP/1.0 %d %s\r\nContent-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %zu\r\n\r\n",
        code, code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request", strlen(body));
    write(fd, h, (size_t)n);
    write(fd, body, strlen(body));
}

static void result(int fd, int err)
{
    char b[160];
    if (!err) reply(fd, 200, "{\"ok\":true}");
    else { snprintf(b, sizeof b, "{\"ok\":false,\"error\":\"%s\"}", strerror(err)); reply(fd, 400, b); }
}

static void handle(int fd)
{
    char req[2048];
    ssize_t r = read(fd, req, sizeof req - 1);
    if (r <= 0) return;
    req[r] = 0;
    char method[8], target[512];
    if (sscanf(req, "%7s %511s", method, target) != 2) return;
    char *q = strchr(target, '?');
    if (q) *q++ = 0;
    int post = !strcmp(method, "POST");
    char a[16], b2[16], cmd[64];

    if (!strcmp(target, "/api/status")) {
        static char body[16384];
        status(body, sizeof body);
        reply(fd, 200, body);
    } else if (post && !strcmp(target, "/api/start")) {
        if (!query(q, "rate", a, sizeof a) || !query(q, "vtbi", b2, sizeof b2)) { result(fd, EINVAL); return; }
        snprintf(cmd, sizeof cmd, "start %s %s\n", a, b2);
        result(fd, pump_write(cmd));
    } else if (post && (!strcmp(target, "/api/pause") || !strcmp(target, "/api/resume") || !strcmp(target, "/api/stop"))) {
        snprintf(cmd, sizeof cmd, "%s\n", target + 5);
        result(fd, pump_write(cmd));
    } else if (post && !strcmp(target, "/api/fault/kill")) {
        result(fd, fault(SIGKILL, EV_FAULT_KILL));
    } else if (post && !strcmp(target, "/api/fault/hang")) {
        result(fd, fault(SIGSTOP, EV_FAULT_HANG));
    } else if (post && !strcmp(target, "/api/load")) {
        result(fd, query(q, "mode", a, sizeof a) ? load(a) : EINVAL);
    } else {
        reply(fd, 404, "{\"ok\":false,\"error\":\"not found\"}");
    }
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (argc > 2) loadgen_path = argv[2];
    set_thread_fifo(PRIO_GATEWAY);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    sh = shm_map();
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    if (!sh || bind(s, (struct sockaddr *)&addr, sizeof addr) || listen(s, 16)) { perror("gateway"); return 1; }
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        struct timeval tv = { 2, 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        handle(c);
        close(c);
    }
}
