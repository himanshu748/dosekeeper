/* Dose controller. A 100 Hz SCHED_FIFO control thread drives the motor by message
 * passing; a resource manager publishes /dev/pump for operators:
 *   echo "start 120 5" > /dev/pump    # 120 mL/h, 5 mL to be infused
 *   cat /dev/pump                     # JSON status
 * The delivered volume is never kept in this process: it is read back from the motor
 * encoder, and the prescription is checkpointed in shared memory, so a restarted
 * controller resumes the same dose without losing or repeating a drop. */
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include <time.h>
#include <unistd.h>

static struct shared *sh;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* priority inheritance by default */
static struct rx rx;
static uint32_t gen;
static uint64_t boot_ns;
static struct {
    int motor_ok;
    uint64_t pos, motor_rate, ticks, misses, late_max, late_win, late_win_prev;
    uint32_t hw_trips;
} st;

static uint64_t delivered(void) { return rx.rx_id && st.pos > rx.start_pos_nl ? st.pos - rx.start_pos_nl : 0; }
static uint64_t limit(void) { return rx.start_pos_nl + rx.vtbi_nl; }

static void commit(uint32_t state)
{
    rx.state = state;
    rx.rx_id++;
    rx_commit(&sh->rx, &rx);
}

static int connect_named(const char *name)
{
    return name_open(name, 0);
}

static void *control(void *arg)
{
    (void)arg;
    set_thread_fifo(PRIO_CONTROL);
    int chid = ChannelCreate(_NTO_CHF_PRIVATE);
    int self = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    struct sigevent sev;
    SIGEV_PULSE_INIT(&sev, self, PRIO_CONTROL, PULSE_TICK, 0);
    timer_t tid;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    struct itimerspec its = { { 0, CONTROL_PERIOD_NS }, { 0, CONTROL_PERIOD_NS } };
    timer_settime(tid, 0, &its, NULL);

    int motor = -1, guard = -1, recovered = 0;
    uint64_t expected = now_ns() + CONTROL_PERIOD_NS;
    struct _pulse p;

    for (;;) {
        if (MsgReceivePulse(chid, &p, sizeof p, NULL) == -1) continue;
        uint64_t t = now_ns();
        uint64_t late = t > expected ? t - expected : 0;
        expected = late > CONTROL_PERIOD_NS ? t + CONTROL_PERIOD_NS : expected + CONTROL_PERIOD_NS;

        if (motor == -1) motor = connect_named(MOTOR_NAME);
        if (guard == -1) guard = connect_named(GUARDIAN_NAME);

        pthread_mutex_lock(&mu);
        struct motor_msg m = { .type = MSG_MOTOR };
        if (rx.state == ST_RUNNING) { m.rate_nl_h = RATE_X10_TO_NL_H(rx.rate_x10); m.limit_nl = limit(); }
        else { m.rate_nl_h = 0; m.limit_nl = st.pos; }
        pthread_mutex_unlock(&mu);

        struct motor_reply r;
        int ok = motor != -1 && MsgSend(motor, &m, sizeof m, &r, sizeof r) == 0;

        pthread_mutex_lock(&mu);
        st.ticks++;
        if (late > st.late_win) st.late_win = late;
        if (late > st.late_max) st.late_max = late;
        if (st.ticks % 100 == 0) { st.late_win_prev = st.late_win; st.late_win = 0; }
        if (late > DEADLINE_SLACK_NS) { st.misses++; ev_push(sh, EV_DEADLINE_MISS, (int64_t)late / 1000, 0); }

        if (!ok) {
            if (st.motor_ok) ev_push(sh, EV_MOTOR_LOST, 0, 0);
            st.motor_ok = 0;
            if (motor != -1) { name_close(motor); motor = -1; }
            if (rx.state == ST_RUNNING) commit(ST_ALARM);
        } else {
            st.motor_ok = 1;
            st.pos = r.pos_nl; st.motor_rate = r.rate_nl_h; st.hw_trips = r.hw_trips;
            if (rx.state == ST_RUNNING && r.pos_nl >= limit()) {
                commit(ST_DONE);
                ev_push(sh, EV_DONE, (int64_t)delivered(), 0);
            }
            if (!recovered) {
                recovered = 1;
                uint64_t base = sh->fault_ns ? sh->fault_ns : sh->death_ns;
                if (base && t > base) {
                    uint64_t us = (t - base) / 1000;
                    sh->last_recovery_us = us;
                    if (us > sh->max_recovery_us) sh->max_recovery_us = us;
                    ev_push(sh, EV_RESTARTED, (int64_t)us, (int64_t)delivered());
                }
                sh->fault_ns = 0; sh->death_ns = 0;
            }
        }
        pthread_mutex_unlock(&mu);

        if (guard != -1 && MsgSendPulse(guard, -1, PULSE_HB, (int)gen) == -1) { name_close(guard); guard = -1; }
    }
    return NULL;
}

static int status_json(char *b, size_t n)
{
    pthread_mutex_lock(&mu);
    int len = snprintf(b, n,
        "{\"state\":\"%s\",\"rate_ml_h\":%.1f,\"vtbi_nl\":%llu,\"delivered_nl\":%llu,"
        "\"encoder_nl\":%llu,\"motor_rate_ml_h\":%.1f,\"motor_ok\":%s,\"hw_trips\":%u,"
        "\"pid\":%d,\"generation\":%u,\"restarts\":%u,\"uptime_ms\":%llu,"
        "\"last_recovery_us\":%llu,\"max_recovery_us\":%llu,"
        "\"ticks\":%llu,\"deadline_misses\":%llu,\"late_max_us\":%llu,\"late_1s_us\":%llu,"
        "\"period_us\":%llu,\"rx_id\":%llu}\n",
        state_name((int)rx.state), rx.rate_x10 / 10.0, (unsigned long long)rx.vtbi_nl,
        (unsigned long long)delivered(), (unsigned long long)st.pos, st.motor_rate / 1e6,
        st.motor_ok ? "true" : "false", st.hw_trips, getpid(), gen, sh->restarts,
        (unsigned long long)((now_ns() - boot_ns) / 1000000),
        (unsigned long long)sh->last_recovery_us, (unsigned long long)sh->max_recovery_us,
        (unsigned long long)st.ticks, (unsigned long long)st.misses,
        (unsigned long long)(st.late_max / 1000),
        (unsigned long long)((st.late_win > st.late_win_prev ? st.late_win : st.late_win_prev) / 1000),
        (unsigned long long)(CONTROL_PERIOD_NS / 1000), (unsigned long long)rx.rx_id);
    pthread_mutex_unlock(&mu);
    return len < (int)n ? len : (int)n - 1;
}

static int apply(const struct cmd *c)
{
    int err = EOK;
    pthread_mutex_lock(&mu);
    switch (c->kind) {
    case CMD_START:
        if (rx.state == ST_RUNNING || rx.state == ST_PAUSED) { err = EBUSY; break; }
        if (!st.motor_ok) { err = EAGAIN; break; }
        rx.rate_x10 = c->rate_x10;
        rx.vtbi_nl = (uint64_t)c->vtbi_x10 * (NL_PER_ML / 10);
        rx.start_pos_nl = st.pos;
        commit(ST_RUNNING);
        ev_push(sh, EV_START, c->rate_x10, (int64_t)rx.vtbi_nl);
        break;
    case CMD_PAUSE:
        if (rx.state != ST_RUNNING) { err = EINVAL; break; }
        commit(ST_PAUSED); ev_push(sh, EV_PAUSE, (int64_t)delivered(), 0);
        break;
    case CMD_RESUME:
        if (rx.state != ST_PAUSED) { err = EINVAL; break; }
        commit(ST_RUNNING); ev_push(sh, EV_RESUME, (int64_t)delivered(), 0);
        break;
    case CMD_STOP:
        if (rx.state != ST_RUNNING && rx.state != ST_PAUSED && rx.state != ST_ALARM) { err = EINVAL; break; }
        commit(ST_IDLE); ev_push(sh, EV_STOP, (int64_t)delivered(), 0);
        break;
    }
    pthread_mutex_unlock(&mu);
    return err;
}

static int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
    int s = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (s != EOK) return s;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    char buf[1024];
    size_t len = (size_t)status_json(buf, sizeof buf);
    size_t off = ocb->offset < 0 ? len : (size_t)ocb->offset;
    size_t nb = off < len ? len - off : 0;
    if (nb > _IO_READ_GET_NBYTES(msg)) nb = _IO_READ_GET_NBYTES(msg);
    ocb->offset += (off_t)nb;
    MsgReply(ctp->rcvid, (long)nb, buf + off, nb);
    return _RESMGR_NOREPLY;
}

static int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int s = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (s != EOK) return s;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    size_t nb = _IO_WRITE_GET_NBYTES(msg);
    char buf[128];
    if (nb == 0 || nb >= sizeof buf) return EINVAL;
    if (resmgr_msgget(ctp, buf, nb, sizeof msg->i) != (ssize_t)nb) return EIO;
    struct cmd c;
    if (parse_cmd(buf, nb, &c)) return EINVAL;
    if ((s = apply(&c)) != EOK) return s;
    _IO_SET_WRITE_NBYTES(ctp, nb);
    return EOK;
}

int main(void)
{
    boot_ns = now_ns();
    sh = shm_map();
    if (!sh) { perror("pumpd: shm"); return 1; }
    gen = __atomic_add_fetch(&sh->generation, 1, __ATOMIC_SEQ_CST);
    if (rx_load(&sh->rx, &rx) == 0) {
        if (rx.state == ST_RUNNING || rx.state == ST_PAUSED) ev_push(sh, EV_RESUMED, (int64_t)rx.rx_id, 0);
    } else {
        memset(&rx, 0, sizeof rx);
        ev_push(sh, EV_BOOT, 0, 0);
    }

    static resmgr_connect_funcs_t cf;
    static resmgr_io_funcs_t iof;
    static iofunc_attr_t attr;
    resmgr_attr_t ra;
    memset(&ra, 0, sizeof ra);
    ra.nparts_max = 1;
    ra.msg_max_size = 2048;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cf, _RESMGR_IO_NFUNCS, &iof);
    iof.read = io_read;
    iof.write = io_write;
    iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);

    dispatch_t *dpp = dispatch_create();
    if (!dpp || resmgr_attach(dpp, &ra, "/dev/pump", _FTYPE_ANY, 0, &cf, &iof, &attr) == -1) {
        perror("pumpd: resmgr_attach");
        return 1;
    }

    pthread_t ct;
    pthread_create(&ct, NULL, control, NULL);

    set_thread_fifo(PRIO_RESMGR);
    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    for (;;) {
        if ((ctp = dispatch_block(ctp)) == NULL) continue;
        dispatch_handler(ctp);
    }
}
