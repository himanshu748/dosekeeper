/* CPU hog used to prove the controller keeps its deadlines.
 *   loadgen rogue    <seconds> <threads>  SCHED_FIFO above the controller: starves it.
 *   loadgen budgeted <seconds> <threads>  SCHED_SPORADIC: 2 ms per 10 ms at high priority,
 *                                         then drops below the controller until replenished. */
#include "common.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t until;

static void *spin(void *arg)
{
    (void)arg;
    volatile uint64_t x = 0;
    while (now_ns() < until) for (int i = 0; i < 10000; i++) x++;
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: loadgen rogue|budgeted seconds threads\n"); return 2; }
    int budgeted = !strcmp(argv[1], "budgeted");
    int secs = atoi(argv[2]), n = atoi(argv[3]);
    if (secs < 1 || secs > 120 || n < 1 || n > 64) return 2;
    until = now_ns() + (uint64_t)secs * 1000000000ULL;

    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    struct sched_param p;
    memset(&p, 0, sizeof p);
    p.sched_priority = PRIO_LOAD_HIGH;
    if (budgeted) {
        pthread_attr_setschedpolicy(&a, SCHED_SPORADIC);
        p.sched_ss_low_priority = PRIO_LOAD_LOW;
        p.sched_ss_max_repl = 8;
        p.sched_ss_repl_period.tv_nsec = 10000000;   /* 10 ms period */
        p.sched_ss_init_budget.tv_nsec = 2000000;    /*  2 ms budget */
    } else {
        pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    }
    pthread_attr_setschedparam(&a, &p);

    pthread_t t[64];
    for (int i = 0; i < n; i++)
        if (pthread_create(&t[i], &a, spin, NULL)) { perror("loadgen: pthread_create"); return 1; }
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    return 0;
}
