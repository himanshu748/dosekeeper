/* Layout shared by every DoseKeeper process on the QNX target. */
#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include "dose.h"

#define MOTOR_NAME     "dosekeeper/motor"
#define GUARDIAN_NAME  "dosekeeper/guardian"
#define SHM_NAME       "/dosekeeper"
#define SHM_MAGIC      0x444B5031u          /* "DKP1" */

#define PRIO_MOTOR     62   /* simulated hardware: always runs */
#define PRIO_GUARDIAN  55
#define PRIO_CONTROL   50
#define PRIO_RESMGR    20
#define PRIO_GATEWAY   10
#define PRIO_LOAD_HIGH 60   /* rogue load sits above the controller */
#define PRIO_LOAD_LOW  8    /* budgeted load drops here once its budget is spent */

#define CONTROL_PERIOD_NS   10000000ULL     /* 100 Hz control loop */
#define DEADLINE_SLACK_NS    5000000ULL     /* a tick later than this is a deadline miss */
#define MOTOR_PERIOD_NS      1000000ULL
#define HW_WATCHDOG_NS     100000000ULL     /* motor stops if not commanded for 100 ms */
#define HB_TIMEOUT_NS       40000000ULL     /* guardian kills a controller silent for 40 ms */
#define START_GRACE_NS     200000000ULL

#define MSG_MOTOR      0x300                /* above _IO_MAX */
#define PULSE_TICK     (_PULSE_CODE_MINAVAIL + 0)
#define PULSE_HB       (_PULSE_CODE_MINAVAIL + 1)

struct motor_msg   { uint16_t type; uint16_t pad[3]; uint64_t rate_nl_h; uint64_t limit_nl; };
struct motor_reply { uint64_t pos_nl; uint64_t rate_nl_h; uint32_t hw_trips; uint32_t at_limit; };

enum ev_kind {
    EV_BOOT = 1, EV_START, EV_PAUSE, EV_RESUME, EV_STOP, EV_DONE, EV_RESUMED,
    EV_FAULT_KILL, EV_FAULT_HANG, EV_DIED, EV_HUNG, EV_RESTARTED, EV_HW_TRIP,
    EV_DEADLINE_MISS, EV_LOAD, EV_MOTOR_LOST
};
struct event { volatile uint64_t seq; uint64_t t_ns; uint32_t kind; uint32_t gen; int64_t a, b; };
#define EV_RING 64

struct shared {
    uint32_t magic;
    struct rx_store rx;              /* prescription checkpoint, written only by pumpd */
    volatile uint64_t fault_ns;      /* gateway: when a fault was injected */
    volatile uint64_t death_ns;      /* guardian: when it noticed the controller was gone */
    volatile uint32_t restarts;      /* guardian */
    volatile int32_t  pumpd_pid;     /* guardian */
    volatile uint32_t generation;    /* pumpd: incremented on every start */
    volatile uint64_t last_recovery_us, max_recovery_us;
    volatile int32_t  load_mode;     /* gateway: 0 off, 1 rogue, 2 budgeted */
    volatile uint64_t load_until_ns;
    volatile uint64_t ev_head;
    struct event ev[EV_RING];
};

uint64_t now_ns(void);
struct shared *shm_map(void);
void ev_push(struct shared *sh, uint32_t kind, int64_t a, int64_t b);
int set_thread_fifo(int prio);
#endif
