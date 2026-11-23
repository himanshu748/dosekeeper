#include "common.h"
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

struct shared *shm_map(void)
{
    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < (off_t)sizeof(struct shared)) ftruncate(fd, sizeof(struct shared));
    struct shared *sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (sh == MAP_FAILED) return NULL;
    uint32_t zero = 0;
    __atomic_compare_exchange_n(&sh->magic, &zero, SHM_MAGIC, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return sh;
}

void ev_push(struct shared *sh, uint32_t kind, int64_t a, int64_t b)
{
    uint64_t i = __atomic_fetch_add(&sh->ev_head, 1, __ATOMIC_SEQ_CST);
    struct event *e = &sh->ev[i % EV_RING];
    e->seq = 0;
    e->t_ns = now_ns(); e->kind = kind; e->gen = sh->generation; e->a = a; e->b = b;
    __atomic_store_n(&e->seq, i + 1, __ATOMIC_RELEASE);
}

int set_thread_fifo(int prio)
{
    struct sched_param p = { .sched_priority = prio };
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
}
