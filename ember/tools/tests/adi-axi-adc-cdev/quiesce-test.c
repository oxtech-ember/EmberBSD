/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the real cdev quiesce function with bounded hardware witnesses. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define MAX(a,b) ((a) > (b) ? (a) : (b))
static unsigned ticks;
#define hz 1000U
#define getticks() ticks
#define mstohz(value) (value)
struct adi_axi_dmac_guard { int error; unsigned delay, calls; };
struct converter_ops { int (*quiesce_rx)(void *, unsigned); };
struct converter { const struct converter_ops *c_ops; void *c_cookie; };
struct adi_axi_adc_lease {
 struct converter *l_converter;
 unsigned l_nguards;
 struct adi_axi_dmac_guard *l_guards[4];
};
struct adi_axi_adc_softc { struct adi_axi_adc_lease *sc_lease; };
int adi_axi_dmac_guard_wait_idle(const struct adi_axi_dmac_guard *, unsigned);
int adi_axi_dmac_guard_wait_idle(const struct adi_axi_dmac_guard *input, unsigned timeout)
{
 struct adi_axi_dmac_guard *g = (struct adi_axi_dmac_guard *)(uintptr_t)input;
 assert(timeout > 0); g->calls++;
 if (g->delay >= timeout) { ticks += timeout; return ETIMEDOUT; }
 ticks += g->delay; return g->error;
}
#include "quiesce.inc"
int main(void)
{
 const struct converter_ops ops = {0};
 struct converter converter = { .c_ops = &ops };
 struct adi_axi_dmac_guard a = {0}, b = {0};
 struct adi_axi_adc_lease lease = { .l_converter=&converter, .l_nguards=2, .l_guards={&a,&b} };
 struct adi_axi_adc_softc sc = { .sc_lease=&lease };
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == 0);
 assert(a.calls == 1 && b.calls == 1);
 a.error=EOPNOTSUPP; b.calls=0;
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == EOPNOTSUPP && b.calls == 0);
 a.error=0; a.delay=6; b.delay=6; ticks=0;
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == ETIMEDOUT && ticks == 10);
 ticks=UINT32_MAX-2; a.delay=2; b.delay=1;
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == 0);
 lease.l_nguards=0;
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == ENXIO);
 sc.sc_lease=NULL;
 assert(adiaxiadc_cdev_quiesce(&sc, 10) == ENXIO);
 puts("PASS actual cdev quiesce: all hardware guards, refusal, shared deadline and tick wrap");
 return 0;
}
