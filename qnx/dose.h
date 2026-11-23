/* Pure, OS-independent dose logic shared by the QNX processes and the host unit tests. */
#ifndef DOSE_H
#define DOSE_H
#include <stdint.h>
#include <stddef.h>

#define NL_PER_ML          1000000ULL
#define NS_PER_HOUR        3600000000000ULL
#define RATE_MAX_X10       9999u          /* 999.9 mL/h */
#define VTBI_MAX_ML        1000u
#define RATE_X10_TO_NL_H(r) ((uint64_t)(r) * 100000ULL)   /* 0.1 mL/h = 100 000 nL/h */

enum pump_state { ST_IDLE = 0, ST_RUNNING, ST_PAUSED, ST_DONE, ST_ALARM };
const char *state_name(int st);

/* Simulated peristaltic motor: integer integration, hard position limit. */
struct motor {
    uint64_t pos_nl;      /* encoder: volume pushed since power-on */
    uint64_t frac;        /* remainder in nL*ns/h units */
    uint64_t rate_nl_h;   /* commanded rate */
    uint64_t limit_nl;    /* hardware interlock: never move past this */
};
/* Advance the motor by dt_ns. Returns 1 if it hit the limit and stopped. */
int motor_advance(struct motor *m, uint64_t dt_ns);

/* Operator commands written to /dev/pump. */
enum cmd_kind { CMD_START = 1, CMD_PAUSE, CMD_RESUME, CMD_STOP };
struct cmd { int kind; uint32_t rate_x10; uint32_t vtbi_x10; };
/* "start <rate mL/h> <vtbi mL>", "pause", "resume", "stop". Returns 0 or -1 if invalid. */
int parse_cmd(const char *buf, size_t len, struct cmd *out);

/* Prescription checkpoint, double-buffered and CRC-protected so a crash mid-write is harmless. */
struct rx {
    uint32_t state;
    uint32_t rate_x10;
    uint64_t vtbi_nl;
    uint64_t start_pos_nl;   /* encoder value when this prescription started */
    uint64_t rx_id;
    uint32_t crc;
    uint32_t pad;
};
struct rx_store { struct rx slot[2]; volatile uint32_t active; };
uint32_t crc32_buf(const void *p, size_t n);
void rx_commit(struct rx_store *s, const struct rx *r);
/* Returns 0 and fills r with the newest valid slot, or -1 if none is valid. */
int rx_load(const struct rx_store *s, struct rx *r);

#endif
