/* Origin: EmberBSD native G2 USB-only fixture interface, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef G2_TEST_H
#define G2_TEST_H
#include <sys/types.h>
struct g2_record { unsigned pipes, xfers, writes, controls; int blocked, ready, closing; };
void rump_g2_ready(struct g2_record *);
void rump_g2_snapshot(struct g2_record *);
void rump_g2_gate(int);
void rump_g2_short(void);
int rump_g2_remove(void);
#endif
