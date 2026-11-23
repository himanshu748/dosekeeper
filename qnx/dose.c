#include "dose.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

const char *state_name(int st)
{
    static const char *n[] = { "IDLE", "RUNNING", "PAUSED", "DONE", "ALARM" };
    return (st >= 0 && st <= ST_ALARM) ? n[st] : "?";
}

int motor_advance(struct motor *m, uint64_t dt_ns)
{
    if (m->rate_nl_h == 0) return 0;
    unsigned __int128 acc = (unsigned __int128)m->rate_nl_h * dt_ns + m->frac;
    uint64_t step = (uint64_t)(acc / NS_PER_HOUR);
    m->frac = (uint64_t)(acc % NS_PER_HOUR);
    if (m->pos_nl + step >= m->limit_nl) {
        m->pos_nl = m->limit_nl > m->pos_nl ? m->limit_nl : m->pos_nl;
        m->rate_nl_h = 0;
        m->frac = 0;
        return 1;
    }
    m->pos_nl += step;
    return 0;
}

/* Parses a decimal with at most one fractional digit into tenths. */
static int parse_tenths(const char **p, uint32_t max, uint32_t *out)
{
    const char *s = *p;
    uint64_t v = 0; int digits = 0;
    while (isdigit((unsigned char)*s)) { v = v * 10 + (uint64_t)(*s++ - '0'); if (++digits > 5) return -1; }
    if (!digits) return -1;
    v *= 10;
    if (*s == '.') {
        s++;
        if (!isdigit((unsigned char)*s)) return -1;
        v += (uint64_t)(*s++ - '0');
        if (isdigit((unsigned char)*s)) return -1;
    }
    if (v == 0 || v > max) return -1;
    *out = (uint32_t)v; *p = s;
    return 0;
}

int parse_cmd(const char *buf, size_t len, struct cmd *out)
{
    char line[128];
    if (len == 0 || len >= sizeof line) return -1;
    memcpy(line, buf, len); line[len] = 0;
    while (len && isspace((unsigned char)line[len - 1])) line[--len] = 0;
    memset(out, 0, sizeof *out);
    if (!strcmp(line, "pause"))  { out->kind = CMD_PAUSE;  return 0; }
    if (!strcmp(line, "resume")) { out->kind = CMD_RESUME; return 0; }
    if (!strcmp(line, "stop"))   { out->kind = CMD_STOP;   return 0; }
    if (strncmp(line, "start ", 6)) return -1;
    const char *p = line + 6;
    while (*p == ' ') p++;
    if (parse_tenths(&p, RATE_MAX_X10, &out->rate_x10)) return -1;
    if (*p != ' ') return -1;
    while (*p == ' ') p++;
    if (parse_tenths(&p, VTBI_MAX_ML * 10, &out->vtbi_x10)) return -1;
    if (*p) return -1;
    out->kind = CMD_START;
    return 0;
}

uint32_t crc32_buf(const void *p, size_t n)
{
    const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *b++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}

static uint32_t rx_crc(const struct rx *r) { return crc32_buf(r, offsetof(struct rx, crc)); }

void rx_commit(struct rx_store *s, const struct rx *r)
{
    uint32_t next = !s->active;
    s->slot[next] = *r;
    s->slot[next].crc = rx_crc(r);
    __atomic_store_n(&s->active, next, __ATOMIC_RELEASE);
}

int rx_load(const struct rx_store *s, struct rx *r)
{
    uint32_t a = __atomic_load_n(&s->active, __ATOMIC_ACQUIRE) & 1;
    for (int i = 0; i < 2; i++) {
        const struct rx *c = &s->slot[i ? !a : a];
        if (c->crc == rx_crc(c) && c->rx_id) { *r = *c; return 0; }
    }
    return -1;
}
