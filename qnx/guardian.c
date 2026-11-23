/* Watchdog: spawns pumpd, restarts it the moment it dies, and kills it if its
 * heartbeat pulses stop for HB_TIMEOUT_NS (a hung controller is a dead controller). */
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include <time.h>

extern char **environ;
static struct shared *sh;
static const char *path;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pid_t child = -1;
static uint64_t started, last_hb;
static int hang_killed;

static void spawn_pumpd(void)
{
    char *argv[] = { (char *)path, NULL };
    pid_t pid;
    if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0) { child = -1; return; }
    child = pid;
    sh->pumpd_pid = pid;
    started = last_hb = now_ns();
    hang_killed = 0;
}

static void *reaper(void *arg)
{
    (void)arg;
    for (;;) {
        int status;
        pid_t p = waitpid(-1, &status, 0);
        if (p < 0) { struct timespec ts = { 0, 10000000 }; nanosleep(&ts, NULL); continue; }
        pthread_mutex_lock(&mu);
        if (p == child) {
            if (!sh->death_ns) sh->death_ns = now_ns();
            ev_push(sh, EV_DIED, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            sh->restarts++;
            spawn_pumpd();
        }
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    path = argc > 1 ? argv[1] : "/proc/boot/pumpd";
    set_thread_fifo(PRIO_GUARDIAN);
    sh = shm_map();
    name_attach_t *att = name_attach(NULL, GUARDIAN_NAME, 0);
    if (!sh || !att) { perror("guardian: setup"); return 1; }

    int self = ConnectAttach(0, 0, att->chid, _NTO_SIDE_CHANNEL, 0);
    struct sigevent sev;
    SIGEV_PULSE_INIT(&sev, self, PRIO_GUARDIAN, PULSE_TICK, 0);
    timer_t tid;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    struct itimerspec its = { { 0, 10000000 }, { 0, 10000000 } };
    timer_settime(tid, 0, &its, NULL);

    pthread_mutex_lock(&mu);
    spawn_pumpd();
    pthread_mutex_unlock(&mu);
    pthread_t rt;
    pthread_create(&rt, NULL, reaper, NULL);

    union { struct _pulse p; uint16_t type; } msg;
    for (;;) {
        rcvid_t rcvid = MsgReceive(att->chid, &msg, sizeof msg, NULL);
        if (rcvid == -1) continue;
        if (rcvid > 0) { MsgReply(rcvid, msg.type == _IO_CONNECT ? EOK : ENOSYS, NULL, 0); continue; }
        uint64_t t = now_ns();
        pthread_mutex_lock(&mu);
        if (msg.p.code == PULSE_HB) {
            last_hb = t;
        } else if (msg.p.code == PULSE_TICK) {
            if (child > 0 && !hang_killed && t - started > START_GRACE_NS && t - last_hb > HB_TIMEOUT_NS) {
                sh->death_ns = t;
                ev_push(sh, EV_HUNG, (int64_t)(t - last_hb) / 1000000, child);
                kill(child, SIGKILL);
                hang_killed = 1;
            }
        } else if (msg.p.code == _PULSE_CODE_DISCONNECT) {
            ConnectDetach(msg.p.scoid);
        }
        pthread_mutex_unlock(&mu);
    }
}
