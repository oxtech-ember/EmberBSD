/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: original actual-source VPU probe contracts, AI-assisted. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>
#include "cixvpu_resources.h"
static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
#ifdef CIXVPU_CONTROL_API
/* Real helper and resource validator; only ACPI, provider and bus services modeled. */
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *device_t;
struct acpi_devnode { ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
struct cixclk { bool claimed, dirty; };
struct cixreset { bool claimed, dirty; };
static char tokens[10];
#define VPU ((void *)&tokens[0])
#define CLOCK ((void *)&tokens[1])
#define RESET ((void *)&tokens[2])
#define POWER ((void *)&tokens[3])
#define OTHER ((void *)&tokens[4])
static struct cixclk clock_claim;
static struct cixreset main_claim, rcsu_claim;
static char events[256];
static unsigned int nevents, allocations, live, bad, faults, sta_count;
static bool interrupt_context, power_live, rc_asserted, clock_live;
static unsigned int delays, reads, maps, unmaps;
static uint64_t clock_rate;
static uint32_t id, ncores, nlsid;
enum {
    F_CLOCK_GET = 1U << 0, F_RESET_GET = 1U << 1, F_RCSU_GET = 1U << 2,
    F_RATE = 1U << 3, F_RCSU_STATE = 1U << 4, F_POWER_ON = 1U << 5,
    F_CLOCK_ON = 1U << 6, F_RESET_ASSERT = 1U << 7,
    F_RESET_DEASSERT = 1U << 8, F_MAP = 1U << 9,
    F_CLOCK_OFF = 1U << 10, F_POWER_OFF = 1U << 11,
    F_STA_ON = 1U << 12, F_STA_OFF = 1U << 13,
    F_STA_ON_FAIL = 1U << 14, F_STA_OFF_FAIL = 1U << 15,
    F_PUT_MAIN = 1U << 16, F_PUT_RCSU = 1U << 17, F_PUT_CLOCK = 1U << 18, F_POWER_NO_LEASE = 1U << 19
};
static void event(char e)
{ assert(nevents + 1 < sizeof(events)); events[nevents++] = e; events[nevents] = 0; }
static void *allocate(size_t n)
{ void *p = calloc(1, n); assert(p); live++; allocations++; return p; }
void AcpiOsFree(void *p) { if (p) { assert(live); live--; free(p); } }
static bool cpu_intr_p(void) { return interrupt_context; }
static bool cpu_softintr_p(void) { return false; }
static void print_error(device_t d, const char *fmt, ...)
{ (void)d; (void)fmt; }
#define aprint_error_dev print_error
struct blob { ACPI_OBJECT o[16]; char strings[64]; };
static void package(ACPI_OBJECT *o, unsigned int n, ACPI_OBJECT *p)
{ o->Type = ACPI_TYPE_PACKAGE; o->Package.Count = n; o->Package.Elements = p; }
static void integer(ACPI_OBJECT *o, uint64_t n)
{ o->Type = ACPI_TYPE_INTEGER; o->Integer.Value = n; }
static void reference(ACPI_OBJECT *o, ACPI_HANDLE h)
{ o->Type = ACPI_TYPE_LOCAL_REFERENCE; o->Reference.Handle = h; }
static void string(ACPI_OBJECT *o, char *dst, const char *text)
{ o->Type = ACPI_TYPE_STRING; strcpy(dst, text); o->String.Pointer = dst; o->String.Length = (UINT32)strlen(text); }
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h, const char *name,
    ACPI_OBJECT_LIST *args, ACPI_BUFFER *result)
{
    struct blob *b;
    assert(h == VPU && args == NULL && result != NULL);
    if (bad == 26) return AE_ERROR;
    if (bad == 29) { result->Pointer = NULL; result->Length = 0; return AE_OK; }
    b = allocate(sizeof(*b));
    result->Pointer = b; result->Length = sizeof(*b);
    if (bad == 39) return AE_ERROR;
    if (strcmp(name, "CLKT") == 0) {
        package(&b->o[0], 1, &b->o[1]); package(&b->o[1], 3, &b->o[2]);
        integer(&b->o[2], 0x43); string(&b->o[3], b->strings, "vpu_clk"); reference(&b->o[4], VPU);
        switch (bad) {
        case 1: integer(&b->o[0], 0); break;
        case 2: b->o[0].Package.Count = 2; break;
        case 3: b->o[1].Package.Count = 2; break;
        case 4: b->o[2].Integer.Value = 0x44; break;
        case 5: b->strings[0] = 'x'; break;
        case 6: b->o[4].Reference.Handle = OTHER; break;
        case 7: b->o[1].Package.Elements = (void *)((uintptr_t)b + sizeof(*b) + 16); break;
        case 8: b->o[3].String.Pointer = (void *)((uintptr_t)b + sizeof(*b) - 2); break;
        case 9: result->Length = sizeof(ACPI_OBJECT) - 1; break;
        case 30: b->o[1].Package.Elements = (void *)((uintptr_t)&b->o[2] + 1); break;
        case 31: b->o[3].String.Length = UINT32_MAX; break;
        case 32: b->o[1].Package.Count = UINT32_MAX; break;
        case 38: b->o[0].Package.Elements = NULL; break;
        default: break;
        }
    } else if (strcmp(name, "RSTL") == 0) {
        package(&b->o[0], 2, &b->o[1]);
        package(&b->o[1], 4, &b->o[3]); package(&b->o[2], 4, &b->o[7]);
        reference(&b->o[3], RESET); integer(&b->o[4], 0x0e); reference(&b->o[5], VPU);
        string(&b->o[6], b->strings, "vpu_reset");
        reference(&b->o[7], RESET); integer(&b->o[8], 0x8e); reference(&b->o[9], VPU);
        string(&b->o[10], b->strings + 24, "vpu_rcsu_reset");
        switch (bad) {
        case 10: b->o[0].Package.Count = 1; break;
        case 11: b->o[7].Reference.Handle = OTHER; break;
        case 12: b->o[8].Integer.Value = 0x0e; break;
        case 13: b->o[9].Reference.Handle = OTHER; break;
        case 14: b->strings[24] = 'x'; break;
        case 15: b->o[3].Type = ACPI_TYPE_INTEGER; break;
        default: break;
        }
    } else {
        assert(strcmp(name, "_PR0") == 0 || strcmp(name, "_PR3") == 0);
        package(&b->o[0], 1, &b->o[1]); reference(&b->o[1], POWER);
        if (bad == 16) b->o[0].Package.Count = 2;
        if (bad == 17) integer(&b->o[1], 0);
        if (bad == 18 && strcmp(name, "_PR3") == 0) b->o[1].Reference.Handle = OTHER;
    }
    return AE_OK;
}
ACPI_STATUS AcpiGetHandle(ACPI_HANDLE h, const char *p, ACPI_HANDLE *out)
{
    if (h == NULL) { assert(strcmp(p, "\\_SB.CCLK") == 0); if (bad == 25) return AE_NOT_FOUND; *out = CLOCK; }
    else {
        assert(h == VPU);
        if (strcmp(p, "PPRS") == 0) *out = bad == 24 ? OTHER : POWER;
        else if (strcmp(p, "CLKT") == 0) *out = &tokens[6];
        else if (strcmp(p, "RSTL") == 0) *out = &tokens[7];
        else if (strcmp(p, "_PR0") == 0) *out = &tokens[8];
        else { assert(strcmp(p, "_PR3") == 0); *out = &tokens[9]; }
    }
    return AE_OK;
}
ACPI_STATUS AcpiGetObjectInfo(ACPI_HANDLE h, ACPI_DEVICE_INFO **out)
{
    const char *hid;
    ACPI_DEVICE_INFO *info;
    assert(h == CLOCK || h == RESET);
    if (bad == 27) return AE_ERROR;
    hid = h == CLOCK ? (bad == 19 ? "WRONG000" : "CIXHA010") : (bad == 21 ? "WRONG000" : "CIXHA020");
    info = allocate(sizeof(*info) + 9);
    info->InfoSize = sizeof(*info) + 9; info->Type = ACPI_TYPE_DEVICE;
    info->Valid = ACPI_VALID_HID; info->HardwareId.Length = 9;
    info->HardwareId.String = (char *)(info + 1); memcpy(info->HardwareId.String, hid, 9);
    if (bad == 33) info->HardwareId.String = (void *)((uintptr_t)info + info->InfoSize - 1);
    if (bad == 40) info->InfoSize = sizeof(*info) - 1;
    if (bad == 41) info->Valid = 0;
    if (bad == 42) info->Type = ACPI_TYPE_METHOD;
    *out = info;
    return AE_OK;
}
ACPI_STATUS AcpiGetType(ACPI_HANDLE h, ACPI_OBJECT_TYPE *out)
{
    if (h == POWER) *out = bad == 23 ? ACPI_TYPE_DEVICE : ACPI_TYPE_POWER;
    else {
        assert(h == &tokens[6] || h == &tokens[7] || h == &tokens[8] || h == &tokens[9]);
        unsigned int index = (unsigned int)((char *)h - tokens);
        *out = bad == index + 28 ? ACPI_TYPE_METHOD : ACPI_TYPE_PACKAGE;
    }
    return AE_OK;
}
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h, const char *n, ACPI_INTEGER *v)
{
    if (strcmp(n, "_UID") == 0) {
        assert(h == CLOCK || h == RESET);
        if (bad == 28) return AE_ERROR;
        *v = ((h == CLOCK && bad == 20) || (h == RESET && bad == 22)) ? 1 : 0;
        return AE_OK;
    }
    assert(h == POWER && strcmp(n, "_STA") == 0);
    sta_count++;
    if (power_live) { event('T'); if (faults & F_STA_ON_FAIL) return AE_ERROR; *v = faults & F_STA_ON ? 0 : 1; }
    else { event('t'); if (faults & F_STA_OFF_FAIL) return AE_ERROR; *v = faults & F_STA_OFF ? 1 : 0; }
    return AE_OK;
}
static int cixclk_acpi_get(ACPI_HANDLE h, uint32_t n, struct cixclk **out)
{ assert(h == CLOCK && n == 0x43); event('C'); if (faults & F_CLOCK_GET) return ENXIO; assert(!clock_claim.claimed); clock_claim.claimed = true; *out = &clock_claim; return 0; }
static int cixreset_acpi_get(ACPI_HANDLE h, uint32_t n, struct cixreset **out)
{
    struct cixreset *r;
    assert(h == RESET && (n == 0x0e || n == 0x8e)); event(n == 0x0e ? 'M' : 'R');
    if (faults & (n == 0x0e ? F_RESET_GET : F_RCSU_GET)) return EBUSY;
    r = n == 0x0e ? &main_claim : &rcsu_claim; assert(!r->claimed); r->claimed = true; *out = r; return 0;
}
static int cixclk_acpi_get_rate(struct cixclk *c, uint64_t *v)
{ assert(c == &clock_claim && c->claimed); event('G'); if (faults & F_RATE) return EIO; *v = clock_rate; return 0; }
static int cixreset_acpi_get_state(struct cixreset *r, bool *v)
{ assert(r == &rcsu_claim && r->claimed); event('S'); if (faults & F_RCSU_STATE) return EIO; *v = rc_asserted; return 0; }
static ACPI_STATUS acpi_power_res(ACPI_HANDLE h, ACPI_HANDLE owner, bool on)
{
    assert(h == POWER && owner == VPU);
    if (on) { event('P'); if (faults & F_POWER_NO_LEASE) return AE_NOT_FOUND; power_live = true; return faults & F_POWER_ON ? AE_ERROR : AE_OK; }
    event('O'); if (faults & F_POWER_NO_LEASE) return AE_ABORT_METHOD; if (faults & F_POWER_OFF) return AE_ERROR; power_live = false; return AE_OK;
}
static int cixclk_acpi_enable(struct cixclk *c)
{ assert(c == &clock_claim && c->claimed && power_live); event('E'); c->dirty = true; clock_live = true; return faults & F_CLOCK_ON ? EIO : 0; }
static int cixclk_acpi_disable(struct cixclk *c)
{ assert(c == &clock_claim && c->claimed && !main_claim.dirty); event('K'); if (faults & F_CLOCK_OFF) return EIO; c->dirty = false; clock_live = false; return 0; }
static int cixreset_acpi_assert(struct cixreset *r)
{ assert(r == &main_claim && r->claimed); event('A'); r->dirty = true; if (faults & F_RESET_ASSERT) return EIO; r->dirty = false; return 0; }
static int cixreset_acpi_deassert(struct cixreset *r)
{ assert(r == &main_claim && r->claimed && clock_live && power_live); event('D'); r->dirty = true; return faults & F_RESET_DEASSERT ? EIO : 0; }
static int cixreset_acpi_put(struct cixreset *r)
{
    assert(r->claimed && !r->dirty); event(r == &main_claim ? 'm' : 'r');
    if (faults & (r == &main_claim ? F_PUT_MAIN : F_PUT_RCSU)) return EBUSY;
    r->claimed = false; return 0;
}
static int cixclk_acpi_put(struct cixclk *c)
{ assert(c->claimed && !c->dirty); event('c'); if (faults & F_PUT_CLOCK) return EBUSY; c->claimed = false; return 0; }
static void delay(unsigned int usec)
{ assert(usec == 20 && power_live && clock_live); event('d'); delays++; }
static int bus_space_map(bus_space_tag_t t, bus_addr_t a, bus_size_t n, int f, bus_space_handle_t *h)
{ (void)t; assert(a == 0x320000 && n == 0x10000 && f == 0); assert(clock_live && power_live && main_claim.dirty); event('B'); maps++; if (faults & F_MAP) return ENOMEM; *h = 1; return 0; }
static uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t; assert(h == 1 && clock_live && power_live && main_claim.dirty); reads++;
    switch (o) { case 0: event('0'); return id; case 8: event('1'); return ncores; case 0xc: event('2'); return nlsid; case 0x30: event('3'); return 0x12345678; default: abort(); }
}
static void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t n)
{ (void)t; assert(h == 1 && n == 0x10000); event('U'); unmaps++; }
#include "control-header.inc"
#include "control-source.inc"
static struct acpi_devnode node = { VPU };
static struct acpi_attach_args aa = { &node, NULL };
static const struct cixvpu_resources description = {
    .mem = {{0x100000, 0x110000, 0x10000}, {0x300000, 0x320000, 0x10000}},
    .nmem = 2, .nirq = 1, .irq = 358, .irq_trigger = 0, .cca_present = 1, .cca = 0
};
static void fixture(void)
{
    assert(live == 0);
    clock_claim = (struct cixclk){0}; main_claim = (struct cixreset){0}; rcsu_claim = (struct cixreset){0};
    bad = faults = sta_count = nevents = delays = reads = maps = unmaps = 0; events[0] = 0;
    interrupt_context = power_live = clock_live = rc_asserted = false;
    clock_rate = 600000000; id = 0x56621234; ncores = 2; nlsid = 4;
}
static void released(void)
{ CHECK(!clock_claim.claimed && !main_claim.claimed && !rcsu_claim.claimed); CHECK(!power_live && !clock_live && live == 0); }
static void metadata_tests(void)
{
    for (unsigned int i = 1; i <= 42; i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); bad = i;
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) != 0);
        CHECK(nevents == 0 && reads == 0 && maps == 0 && delays == 0 && sta_count == 0);
        released();
    }
    struct cixvpu_acpi_control c = {0}; struct cixvpu_resources invalid = description;
    fixture(); invalid.nmem = 1;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &invalid, true) == EINVAL);
    CHECK(nevents == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; interrupt_context = true;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == EWOULDBLOCK);
    CHECK(nevents == 0); interrupt_context = false;
}
static void query_tests(void)
{
    const unsigned int cases[] = {0, F_CLOCK_GET, F_RESET_GET, F_RCSU_GET, F_RATE};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        int rv = cixvpu_acpi_control_probe(&c, VPU, &aa, &description, false);
        CHECK(i == 0 ? rv == 0 : rv != 0);
        CHECK(reads == 0 && maps == 0 && delays == 0 && sta_count == 0);
        CHECK(strchr(events, 'S') == NULL && strchr(events, 'P') == NULL && strchr(events, 'A') == NULL && strchr(events, 'E') == NULL);
        if (i == 0) { CHECK(strcmp(events, "CMRGrmc") == 0); CHECK(c.report.rate == 600000000); }
        released();
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, false) == EALREADY);
    }
}
static void active_tests(void)
{
    struct cixvpu_acpi_control c = {0}; fixture();
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == 0);
    CHECK(strcmp(events, "CMRSPTEGAdDdB0123UAKOtrmc") == 0);
    CHECK(c.report.id == 0x56621234 && c.report.ncores == 2 && c.report.nlsid == 4 && c.report.svn == 0x12345678);
    CHECK(c.report.rate == 600000000 && c.report.error == 0 && c.report.cleanup_error == 0);
    CHECK(reads == 4 && delays == 2 && maps == 1 && unmaps == 1 && sta_count == 2);
    released();
    const uint32_t ids[] = {0x56500000, 0x56550000, 0x56600000, 0x56610000, 0x56620000, 0x56630000, 0x56640000, 0x56650000, 0};
    for (unsigned int i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) {
        fixture(); c = (struct cixvpu_acpi_control){0}; id = ids[i];
        CHECK((cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == 0) == (i < 7)); released();
    }
    for (unsigned int i = 0; i < 4; i++) {
        fixture(); c = (struct cixvpu_acpi_control){0};
        if (i < 2) ncores = i == 0 ? 0 : 5; else nlsid = i == 2 ? 0 : 5;
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) != 0); released();
    }
}
static void failures_tests(void)
{
    const unsigned int cases[] = {F_RCSU_STATE, F_POWER_ON, F_CLOCK_ON, F_RATE,
        F_RESET_DEASSERT, F_MAP, F_STA_ON, F_STA_ON_FAIL};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) != 0);
        CHECK(c.report.error != 0 && c.report.cleanup_error == 0);
        if (cases[i] == F_RCSU_STATE) CHECK(strchr(events, 'P') == NULL && strchr(events, 'A') == NULL);
        else { const char *a = strchr(events, 'A'), *off = strchr(events, 'O'); CHECK(a != NULL && off != NULL && a < off); CHECK(sta_count >= 1); }
        CHECK(reads == 0); released();
    }
    struct cixvpu_acpi_control c = {0}; fixture(); rc_asserted = true;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == EBUSY);
    CHECK(strcmp(events, "CMRSrmc") == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; clock_rate = 0;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) != 0); CHECK(reads == 0); released();
}
static void retention_tests(void)
{
    const unsigned int cases[] = {F_RESET_ASSERT, F_CLOCK_OFF, F_POWER_OFF, F_STA_OFF, F_STA_OFF_FAIL};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) != 0);
        CHECK(c.report.cleanup_error != 0);
        CHECK(clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
        if (cases[i] == F_RESET_ASSERT) CHECK(strchr(events, 'K') == NULL && strchr(events, 'O') == NULL && power_live && clock_live);
        if (cases[i] == F_CLOCK_OFF) CHECK(strchr(events, 'O') == NULL && power_live && clock_live);
        if (cases[i] == F_POWER_OFF) CHECK(power_live && c.power_attempted);
        if (cases[i] == F_STA_OFF || cases[i] == F_STA_OFF_FAIL) {
            CHECK(!power_live && !c.power_attempted && c.power_released_fault);
            unsigned int previous = nevents; faults = 0;
            CHECK(cixvpu_acpi_control_cleanup(&c) != 0 && nevents == previous);
            CHECK(clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
            continue; /* Fault needs explicit diagnosis; no automatic ON/OFF replay. */
        }
        faults = 0;
        CHECK(cixvpu_acpi_control_cleanup(&c) == 0);
        released();
    }
    struct cixvpu_acpi_control c = {0}; fixture(); faults = F_POWER_ON | F_POWER_OFF;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == EIO);
    CHECK(c.report.error == EIO && c.report.cleanup_error == EIO);
    CHECK(c.report.acpi_status == AE_ERROR && c.report.cleanup_acpi_status == AE_ERROR);
    faults = 0; CHECK(cixvpu_acpi_control_cleanup(&c) == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; faults = F_POWER_NO_LEASE;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, true) == EIO);
    CHECK(c.report.error == EIO && c.report.cleanup_error == EIO);
    CHECK(c.report.acpi_status == AE_NOT_FOUND && c.report.cleanup_acpi_status == AE_ABORT_METHOD);
    CHECK(c.power_attempted && !power_live && clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
    CHECK(reads == 0 && !clock_live && strchr(events, 'E') == NULL);
    CHECK(cixvpu_acpi_control_cleanup(&c) == EIO); /* Remain conservative; do not invent a lease. */
    const unsigned int put_cases[] = {F_PUT_MAIN, F_PUT_RCSU, F_PUT_CLOCK};
    for (unsigned int i = 0; i < sizeof(put_cases)/sizeof(put_cases[0]); i++) {
        fixture(); c = (struct cixvpu_acpi_control){0}; faults = put_cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &aa, &description, false) != 0);
        CHECK(c.report.error == 0 && c.report.cleanup_error == EBUSY);
        CHECK(strchr(events, 'A') == NULL && strchr(events, 'P') == NULL);
        faults = 0; CHECK(cixvpu_acpi_control_cleanup(&c) == 0); released();
    }
}
#endif
int main(void)
{
#ifdef CIXVPU_CONTROL_API
    metadata_tests(); query_tests(); active_tests(); failures_tests(); retention_tests();
    CHECK(live == 0 && allocations != 0);
#else
    CHECK(!"native VPU control helper is absent");
#endif
    printf("cixvpu control actual-source contract: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
