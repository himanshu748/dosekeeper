/* Simulated peristaltic pump motor ("the hardware"). Integrates volume at the commanded
 * rate, refuses to move past the commanded limit, and stops by itself if the controller
 * goes quiet for HW_WATCHDOG_NS. Reached by native message passing as MOTOR_NAME. */
#include "common.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>

int main(void)
{
    set_thread_fifo(PRIO_MOTOR);
    struct shared *sh = shm_map();
    name_attach_t *att = name_attach(NULL, MOTOR_NAME, 0);
    if (!sh || !att) { perror("motor_sim: setup"); return 1; }

    int self = ConnectAttach(0, 0, att->chid, _NTO_SIDE_CHANNEL, 0);
    struct sigevent sev;
    SIGEV_PULSE_INIT(&sev, self, PRIO_MOTOR, PULSE_TICK, 0);
    timer_t tid;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    struct itimerspec its = { { 0, MOTOR_PERIOD_NS }, { 0, MOTOR_PERIOD_NS } };
    timer_settime(tid, 0, &its, NULL);

    struct motor m = { 0 };
    uint64_t last = now_ns(), last_cmd = last;
    uint32_t trips = 0;
    union { struct _pulse p; uint16_t type; struct motor_msg mm; } msg;

    for (;;) {
        rcvid_t rcvid = MsgReceive(att->chid, &msg, sizeof msg, NULL);
        if (rcvid == -1) continue;
        uint64_t t = now_ns();
        motor_advance(&m, t - last);
        last = t;
        if (rcvid == 0) {
            if (msg.p.code == PULSE_TICK && m.rate_nl_h && t - last_cmd > HW_WATCHDOG_NS) {
                m.rate_nl_h = 0;
                trips++;
                ev_push(sh, EV_HW_TRIP, (int64_t)(t - last_cmd) / 1000000, 0);
            } else if (msg.p.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(msg.p.scoid);
            }
            continue;
        }
        if (msg.type == _IO_CONNECT) { MsgReply(rcvid, EOK, NULL, 0); continue; }
        if (msg.type != MSG_MOTOR) { MsgError(rcvid, ENOSYS); continue; }

        m.limit_nl = msg.mm.limit_nl;
        m.rate_nl_h = m.pos_nl < m.limit_nl ? msg.mm.rate_nl_h : 0;
        last_cmd = t;
        struct motor_reply r = { m.pos_nl, m.rate_nl_h, trips, m.pos_nl >= m.limit_nl };
        MsgReply(rcvid, EOK, &r, sizeof r);
    }
}
