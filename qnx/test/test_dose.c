#include "../dose.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int ok(const char *s, struct cmd *c) { return parse_cmd(s, strlen(s), c) == 0; }

int main(void)
{
    struct cmd c;
    assert(ok("start 120 5\n", &c) && c.kind == CMD_START && c.rate_x10 == 1200 && c.vtbi_x10 == 50);
    assert(ok("start 0.5 0.1", &c) && c.rate_x10 == 5 && c.vtbi_x10 == 1);
    assert(ok("start 360.00 6.50", &c) && c.rate_x10 == 3600 && c.vtbi_x10 == 65);
    assert(!ok("start 360.05 6", &c));
    assert(ok("pause", &c) && c.kind == CMD_PAUSE);
    assert(!ok("start 1000 5", &c));      /* above 999.9 mL/h */
    assert(!ok("start 10 1001", &c));     /* above 1000 mL */
    assert(!ok("start 0 5", &c));
    assert(!ok("start 1.25 5", &c));
    assert(!ok("start -1 5", &c));
    assert(!ok("start 10 5 x", &c));
    assert(!ok("bolus 50", &c));

    /* 120 mL/h for 60 s must be exactly 2 mL, integrated in 1 ms steps. */
    struct motor m = { .rate_nl_h = RATE_X10_TO_NL_H(1200), .limit_nl = UINT64_MAX };
    for (int i = 0; i < 60000; i++) motor_advance(&m, 1000000);
    assert(m.pos_nl == 2 * NL_PER_ML);

    /* The interlock stops exactly at the limit even with a huge step. */
    struct motor l = { .rate_nl_h = RATE_X10_TO_NL_H(9999), .limit_nl = 5 * NL_PER_ML };
    assert(motor_advance(&l, 3600ULL * 1000000000ULL) == 1);
    assert(l.pos_nl == 5 * NL_PER_ML && l.rate_nl_h == 0);
    assert(motor_advance(&l, 1000000000ULL) == 0 && l.pos_nl == 5 * NL_PER_ML);

    /* Checkpoint survives a torn write of the inactive slot. */
    struct rx_store s; memset(&s, 0, sizeof s);
    struct rx r = { .state = ST_RUNNING, .rate_x10 = 1200, .vtbi_nl = 5 * NL_PER_ML, .rx_id = 1 }, got;
    assert(rx_load(&s, &got) == -1);
    rx_commit(&s, &r);
    r.rx_id = 2; rx_commit(&s, &r);
    s.slot[!s.active].vtbi_nl ^= 1;                    /* corrupt old slot */
    assert(rx_load(&s, &got) == 0 && got.rx_id == 2);
    s.slot[s.active].rate_x10 ^= 1;                    /* corrupt active slot too */
    assert(rx_load(&s, &got) == -1);
    puts("dose tests passed");
    return 0;
}
