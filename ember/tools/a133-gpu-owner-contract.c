/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD A133 GPU owner state contract, AI-assisted. */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "sun50i_a133_gpu_owner.h"
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct mock { unsigned calls; int fail_at; };
static int step(void *cookie) {
    struct mock *m = cookie; m->calls++;
    return (int)m->calls == m->fail_at ? EIO : 0;
}
int main(void) {
    struct a133_gpu_owner owner;
    struct mock m;
    const struct a133_gpu_owner_ops ops = { step, step, step, step };
    for (int fail = 1; fail <= 4; fail++) {
        owner = (struct a133_gpu_owner){0}; m = (struct mock){0, fail};
        CHECK(a133_gpu_owner_boot(&owner, &ops, &m) == EIO);
        CHECK(m.calls == (unsigned)fail);
        CHECK(owner.attempted == (fail > 1));
        CHECK(owner.retain_resources == (fail > 1));
        if (fail > 1) {
            CHECK(a133_gpu_owner_boot(&owner, &ops, &m) == EBUSY);
            CHECK(m.calls == (unsigned)fail);
        }
    }
    owner = (struct a133_gpu_owner){0}; m = (struct mock){0, 0};
    CHECK(a133_gpu_owner_boot(&owner, &ops, &m) == 0);
    CHECK(owner.attempted && owner.retain_resources && m.calls == 4);
    CHECK(a133_gpu_owner_boot(&owner, &ops, &m) == EBUSY);
    CHECK(m.calls == 4);
    uint64_t effective = 99;
    CHECK(a133_gpu_effective_hz(504000000, 1, &effective) == 0 && effective == 504000000);
    CHECK(a133_gpu_effective_hz(504000000, 2, &effective) == 0 && effective == 472500000);
    CHECK(a133_gpu_effective_hz(504000000, 4, &effective) == 0 && effective == 441000000);
    CHECK(a133_gpu_effective_hz(252000000, 1, &effective) == 0 && effective == 252000000);
    effective = 99;
    CHECK(a133_gpu_effective_hz(504000000, 3, &effective) == EOPNOTSUPP && effective == 99);
    CHECK(a133_gpu_effective_hz(252000000, 2, &effective) == EOPNOTSUPP && effective == 99);
    CHECK(a133_gpu_effective_hz(504000000, 1, NULL) == EINVAL);
    CHECK(a133_gpu_opp_valid(504000000, 950000));
    CHECK(a133_gpu_opp_valid(472500000, 950000));
    CHECK(a133_gpu_opp_valid(441000000, 950000));
    CHECK(a133_gpu_opp_valid(252000000, 950000));
    CHECK(!a133_gpu_opp_valid(504000000, 949999));
    CHECK(!a133_gpu_opp_valid(600000000, 950000));
    puts("A133 GPU owner preflight, retention and one-shot: PASS");
    return 0;
}
