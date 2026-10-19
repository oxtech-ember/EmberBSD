/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted A9 timer registration contract. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL %d: %s\n", \
    __LINE__, #e); abort(); } } while (0)
#define KASSERT(e) CHECK(e)
#define EVCNT_TYPE_MISC 0
#define IPL_CLOCK 0
#define IST_EDGE 1
#define IST_MPSAFE 2
#define CPU_ID_CORTEX_A5_P(id) ((id) == 5)
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)cpu_type)
#define aprint_normal_dev(...) ((void)0)
#define aprint_error_dev(...) ((void)0)
#define panic(...) abort()
typedef unsigned u_int;
typedef unsigned long u_long;
typedef unsigned bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
struct device { uint32_t frequency; };
typedef struct device *device_t;
typedef struct device *prop_dictionary_t;
struct evcnt { unsigned ev_count; };
struct cpu_info { unsigned ci_arm_cpuid; uint64_t ci_lastintr; };
struct mpcore_attach_args {
    bus_space_tag_t mpcaa_memt;
    bus_space_handle_t mpcaa_memh;
    unsigned mpcaa_off1;
    int mpcaa_irq;
};
struct timecounter {
    u_int (*tc_get_timecount)(struct timecounter *);
    void *tc_poll_pps;
    unsigned tc_counter_mask;
    uint32_t tc_frequency;
    const char *tc_name;
    int tc_quality;
    void *tc_priv, *tc_next;
};
#include "a9tmr_reg.h"
#include "a9tmr_var.h"
static struct a9tmr_softc a9tmr_sc;
static struct cpu_info cpu = { .ci_arm_cpuid = 9 };
static unsigned hz = 100, inits, detaches, broadcasts;
static uint32_t regs[TMR_GLOBAL_SIZE / 4];
static bool registered;
static struct cpu_info *curcpu(void) { return &cpu; }
static const char *device_xname(device_t d) { return "timer-test"; }
static prop_dictionary_t device_properties(device_t d) { return d; }
static void device_set_private(device_t d, void *p) { }
static bool
prop_dictionary_get_uint32(prop_dictionary_t d, const char *name, uint32_t *value)
{
    CHECK(strcmp(name, "frequency") == 0);
    if (d->frequency == 0)
        return false;
    *value = d->frequency;
    return true;
}
static void humanize_number(char *p, size_t n, uint32_t f, const char *u, int b) { }
static void evcnt_attach_dynamic(void *p, int t, void *q, const char *d, const char *n) { }
static int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t h,
    unsigned offset, unsigned size, bus_space_handle_t *out)
{ *out = h + offset; return 0; }
static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, unsigned reg)
{ CHECK(reg < TMR_GLOBAL_SIZE); return regs[reg / 4]; }
static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, unsigned reg, uint32_t v)
{ CHECK(reg < TMR_GLOBAL_SIZE); regs[reg / 4] = v; }
static int a9tmr_intr(void *arg) { return 1; }
static void *intr_establish(int irq, int ipl, int type, int (*fn)(void *), void *arg)
{ CHECK(fn == a9tmr_intr); return &cpu; }
static void
tc_init(struct timecounter *tc)
{
    CHECK(!registered && tc->tc_frequency != 0);
    CHECK(regs[TMR_GBL_CTL / 4] & TMR_CTL_ENABLE);
    CHECK(strcmp(tc->tc_name, "timer-test") == 0);
    CHECK(tc->tc_get_timecount(tc) == 1234);
    registered = true;
    inits++;
}
static int
tc_detach(struct timecounter *tc)
{ CHECK(registered); registered = false; detaches++; return 0; }
static uint64_t
xc_broadcast(unsigned flags, void (*fn)(void *, void *), void *a, void *b)
{ broadcasts++; fn(a, b); return 1; }
static void xc_wait(uint64_t id) { CHECK(id == 1); }
static u_int a9tmr_get_timecount(struct timecounter *);
#include "timer-bodies.h"

static void
test_timer(bool known, bool own_clock, bool parent_frequency)
{
    struct device parent = {parent_frequency && known ? 1000000 : 0};
    struct device device = {!parent_frequency && known ? 1000000 : 0};
    struct mpcore_attach_args args = {.mpcaa_irq = own_clock ? 27 : -1};

    memset(&a9tmr_sc, 0, sizeof(a9tmr_sc));
    memset(regs, 0, sizeof(regs));
    a9tmr_timecounter.tc_frequency = 0;
    registered = false;
    inits = detaches = broadcasts = 0;
    regs[TMR_GBL_CTR_L / 4] = 1234;
    a9tmr_attach(&parent, &device, &args);
    CHECK(registered == known && inits == (known ? 1U : 0U));
    CHECK(regs[TMR_GBL_CTL / 4] == TMR_CTL_ENABLE);
    CHECK(a9tmr_sc.sc_autoinc == 0);
    if (own_clock) {
        CHECK(known);
        a9tmr_cpu_initclocks();
        CHECK(a9tmr_sc.sc_autoinc == 10000);
        CHECK(inits == 1); /* hardclock startup must not register twice. */
    }
    a9tmr_update_freq(2000000);
    CHECK(registered && a9tmr_timecounter.tc_frequency == 2000000);
    CHECK(inits == (known ? 2U : 1U) && detaches == (known ? 1U : 0U));
    CHECK(broadcasts == (own_clock ? 1U : 0U));
    CHECK(a9tmr_sc.sc_autoinc == (own_clock ? 20000U : 0U));
    if (!own_clock)
        CHECK(regs[TMR_GBL_CTL / 4] == TMR_CTL_ENABLE);
}
int
main(void)
{
    test_timer(true, false, false);
    test_timer(true, false, true);
    test_timer(true, true, false);
    test_timer(false, false, false);
    puts("PASS A9 attach registration, deferred frequency, and hardclock isolation");
    return 0;
}
