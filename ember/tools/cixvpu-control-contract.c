/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: original actual-source VPU probe contracts, AI-assisted. */
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>
#include "cixvpu_resources.h"
#include "cixvpu_fw_plan.h"
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
struct acpi_devnode {
    ACPI_HANDLE ad_handle;
    device_t ad_root;
    ACPI_DEVICE_INFO *ad_devinfo;
    uint32_t ad_type;
    struct acpi_devnode *next;
};
struct acpi_softc { struct { struct acpi_devnode *first; } sc_head; };
#define SIMPLEQ_FOREACH(v,h,f) for ((v)=(h)->first;(v)!=NULL;(v)=(v)->next)
typedef void *bus_dma_tag_t;
struct acpi_attach_args {
    struct acpi_devnode *aa_node;
    bus_space_tag_t aa_memt;
    bus_dma_tag_t aa_dmat, aa_dmat64;
};
struct cixclk { bool claimed, dirty, native; };
struct cixreset { bool claimed, dirty; };
static char tokens[12];
#define VPU ((void *)&tokens[0])
#define CLOCK ((void *)&tokens[1])
#define RESET ((void *)&tokens[2])
#define POWER ((void *)&tokens[3])
#define OTHER ((void *)&tokens[4])
#define ROOT ((void *)&tokens[10])
#define SCMI ((void *)&tokens[11])
static struct acpi_softc acpi_root;
static void *root_private = &acpi_root;
static void *device_private(device_t);
static union { max_align_t align; unsigned char bytes[sizeof(ACPI_DEVICE_INFO) + 30]; } scmi_cache;
static struct { ACPI_DEVICE_INFO *info; char *hid, *uid; } scmi_info;
static struct acpi_devnode scmi_node;
static struct cixclk clock_claim;
static struct cixreset main_claim, rcsu_claim;
static char events[256];
static unsigned int nevents, allocations, live, bad, faults, sta_count;
static bool interrupt_context, power_live, rc_asserted, clock_live;
static bool native_poison;
static unsigned int native_disable_calls;
static unsigned int delays, reads, maps, unmaps;
static ACPI_STATUS attach_cca_status;
static ACPI_INTEGER attach_cca_value;
/* Opaque provider turns SCMI status -4 (NOT_FOUND) into EIO. */
static int32_t rate_status;
static uint64_t clock_rate;
static uint32_t id, ncores, nlsid, svn;
static int cold = 1;
static bool profile_mode;
static uint32_t profile_words[7];
#ifdef CIXVPU_BOOT_ORCH_API
static char boot_core_tokens[16];
#define BOOT_CORE(i) ((void *)&boot_core_tokens[(i)])
#define BOOT_POWER(i) ((void *)&boot_core_tokens[4+(i)])
#define BOOT_TABLE(i,j) ((void *)&boot_core_tokens[8+2*(i)+(j)])
static unsigned int boot_core_bad;
static int boot_core_on_fail = -1, boot_core_off_fail = -1;
static int boot_core_no_lease = -1;
static bool boot_core_live[4];
#endif
#define BUS_SPACE_BARRIER_READ 1
#define F_PROFILE_MAP (1U << 21)
enum {
    F_CLOCK_GET = 1U << 0, F_RESET_GET = 1U << 1, F_RCSU_GET = 1U << 2,
    F_RATE = 1U << 3, F_RCSU_STATE = 1U << 4, F_POWER_ON = 1U << 5,
    F_CLOCK_ON = 1U << 6, F_RESET_ASSERT = 1U << 7,
    F_RESET_DEASSERT = 1U << 8, F_MAP = 1U << 9,
    F_CLOCK_OFF = 1U << 10, F_POWER_OFF = 1U << 11,
    F_STA_ON = 1U << 12, F_STA_OFF = 1U << 13,
    F_STA_ON_FAIL = 1U << 14, F_STA_OFF_FAIL = 1U << 15,
    F_PUT_MAIN = 1U << 16, F_PUT_RCSU = 1U << 17, F_PUT_CLOCK = 1U << 18, F_POWER_NO_LEASE = 1U << 19,
    F_NATIVE_POISON = 1U << 20
};
static void event(char e)
{ assert(nevents + 1 < sizeof(events)); events[nevents++] = e; events[nevents] = 0; }
static void *allocate(size_t n)
{ void *p = calloc(1, n); assert(p); live++; allocations++; return p; }
void AcpiOsFree(void *p) { if (p) { assert(live); live--; free(p); } }
static bool cpu_intr_p(void) { return interrupt_context; }
static bool soft_context;
static bool cpu_softintr_p(void) { return soft_context; }
static char diagnostic[8192];
static void print_error(device_t d, const char *fmt, ...)
{
    va_list ap;
    (void)d;
    size_t used = strlen(diagnostic);
    va_start(ap, fmt);
    if (used < sizeof(diagnostic)) vsnprintf(diagnostic + used, sizeof(diagnostic) - used, fmt, ap);
    va_end(ap);
}
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
#ifdef CIXVPU_BOOT_ORCH_API
    for (unsigned int i = 0; i < 4; i++) {
        if (h != BOOT_CORE(i)) continue;
        assert(args == NULL && result != NULL && (strcmp(name,"_PR0")==0 || strcmp(name,"_PR3")==0));
        b = allocate(sizeof(*b)); result->Pointer=b; result->Length=sizeof(*b);
        package(&b->o[0],1,&b->o[1]); reference(&b->o[1],boot_core_bad==3 ? OTHER : BOOT_POWER(i));
        return AE_OK;
    }
#endif
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
#ifdef CIXVPU_BOOT_ORCH_API
    for (unsigned int i = 0; i < 4; i++) {
        if (h == VPU && strlen(p)==4 && memcmp(p,"CRE",3)==0 && p[3]=='0'+(int)i) {
            *out=BOOT_CORE(i); return AE_OK;
        }
        if (h == BOOT_CORE(i) && strlen(p)==4 && memcmp(p,"PRS",3)==0 && p[3]=='0'+(int)i) {
            *out=BOOT_POWER(i); return AE_OK;
        }
        if (h == BOOT_CORE(i) && (strcmp(p,"_PR0")==0 || strcmp(p,"_PR3")==0)) {
            *out=BOOT_TABLE(i,strcmp(p,"_PR3")==0); return AE_OK;
        }
    }
#endif
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
#ifdef CIXVPU_BOOT_ORCH_API
    for (unsigned int i = 0; i < 4; i++) {
        if (h == BOOT_CORE(i)) { *out=ACPI_TYPE_DEVICE; return AE_OK; }
        if (h == BOOT_POWER(i)) { *out=boot_core_bad==2 ? ACPI_TYPE_DEVICE : ACPI_TYPE_POWER; return AE_OK; }
        if (h == BOOT_TABLE(i,0) || h == BOOT_TABLE(i,1)) { *out=ACPI_TYPE_PACKAGE; return AE_OK; }
    }
#endif
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
#ifdef CIXVPU_BOOT_ORCH_API
    for (unsigned int i = 0; i < 4; i++)
        if (h == BOOT_CORE(i) && strcmp(n,"_ADR")==0) { *v=boot_core_bad==1 ? 9 : i; return AE_OK; }
#endif
    if (h == VPU && strcmp(n, "_CCA") == 0) { *v = attach_cca_value; return attach_cca_status; }
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
static int cixclk_acpi_get_native(ACPI_HANDLE h, ACPI_HANDLE scmi,
    uint32_t n, struct cixclk **out)
{ assert(h == CLOCK && scmi == SCMI && n == 0x43); event('N'); if (faults & F_CLOCK_GET) return ENXIO; assert(!clock_claim.claimed); clock_claim.claimed = clock_claim.native = true; *out = &clock_claim; return 0; }
static int cixreset_acpi_get(ACPI_HANDLE h, uint32_t n, struct cixreset **out)
{
    struct cixreset *r;
    assert(h == RESET && (n == 0x0e || n == 0x8e)); event(n == 0x0e ? 'M' : 'R');
    if (faults & (n == 0x0e ? F_RESET_GET : F_RCSU_GET)) return EBUSY;
    r = n == 0x0e ? &main_claim : &rcsu_claim; assert(!r->claimed); r->claimed = true; *out = r; return 0;
}
static int cixclk_acpi_get_rate(struct cixclk *c, uint64_t *v)
{ assert(c == &clock_claim && c->claimed); event('G'); if ((faults & F_RATE) || rate_status != 0) return EIO; *v = clock_rate; return 0; }
static int cixreset_acpi_get_state(struct cixreset *r, bool *v)
{ assert(r == &rcsu_claim && r->claimed); event('S'); if (faults & F_RCSU_STATE) return EIO; *v = rc_asserted; return 0; }
static ACPI_STATUS acpi_power_res(ACPI_HANDLE h, ACPI_HANDLE owner, bool on)
{
#ifdef CIXVPU_BOOT_ORCH_API
    for (unsigned int i = 0; i < 4; i++) {
        if (h != BOOT_POWER(i)) continue;
        assert(owner == BOOT_CORE(i)); event(on ? (char)('a'+i) : (char)('4'+i));
        if (boot_core_no_lease==(int)i) return on ? AE_NOT_FOUND : AE_ABORT_METHOD;
        bool failed=(on ? boot_core_on_fail : boot_core_off_fail)==(int)i;
        if (on || !failed) boot_core_live[i]=on;
        return failed ? AE_ERROR : AE_OK;
    }
#endif
    assert(h == POWER && owner == VPU);
    if (on) { event('P'); if (faults & F_POWER_NO_LEASE) return AE_NOT_FOUND; power_live = true; return faults & F_POWER_ON ? AE_ERROR : AE_OK; }
    event('O'); if (faults & F_POWER_NO_LEASE) return AE_ABORT_METHOD; if (faults & F_POWER_OFF) return AE_ERROR; power_live = false; return AE_OK;
}
static int cixclk_acpi_enable(struct cixclk *c)
{ assert(c == &clock_claim && c->claimed && power_live); event('E'); c->dirty = true; clock_live = true; if (c->native && (faults & F_NATIVE_POISON)) { native_poison = true; return EIO; } return faults & F_CLOCK_ON ? EIO : 0; }
static int cixclk_acpi_disable(struct cixclk *c)
{ assert(c == &clock_claim && c->claimed && !main_claim.dirty); if (c->native) { native_disable_calls++; if (native_poison) return EIO; } event('K'); if (faults & F_CLOCK_OFF) return EIO; c->dirty = false; clock_live = false; return 0; }
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
{
    (void)t; assert(n == 0x10000 && f == 0);
    assert(clock_live && power_live && main_claim.dirty);
    if (profile_mode && a == 0x14230000) {
        event('V'); maps++; if (faults & F_PROFILE_MAP) return ENOMEM; *h = 2; return 0;
    }
    assert(a == (profile_mode ? 0x14240000U : 0x320000U));
    event('B'); maps++; if (faults & F_MAP) return ENOMEM; *h = 1; return 0;
}
static uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t; assert(clock_live && power_live && main_claim.dirty); reads++;
    if (h == 2) {
        assert(profile_mode);
        switch (o) {
        case 0x308: event('4'); return profile_words[0];
        case 0x300: event('5'); return profile_words[1];
        case 0x21c: event('6'); return profile_words[2];
        case 0x10: event('7'); return profile_words[3];
        case 0x14: event('8'); return profile_words[4];
        case 0x18: event('9'); return profile_words[5];
        default: abort();
        }
    }
    assert(h == 1);
    switch (o) {
    case 0: event('0'); return id;
    case 8: event('1'); return ncores;
    case 0xc: event('2'); return nlsid;
    case 0x30: event('3'); return svn;
    case 0x24: assert(profile_mode); event('F'); return profile_words[6];
    default: abort();
    }
}
static void bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, bus_size_t length, int flags)
{
    (void)t; CHECK(profile_mode && flags == BUS_SPACE_BARRIER_READ);
    CHECK(h == 1 || h == 2); CHECK(offset <= 0x10000 && length <= 0x10000 - offset);
    CHECK(clock_live && power_live && main_claim.dirty); event(h == 2 ? 'q' : 'b');
}
static void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t n)
{ (void)t; assert((h == 1 || (profile_mode && h == 2)) && n == 0x10000); event(h == 2 ? 'X' : 'U'); unmaps++; }
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#include "control-header.inc"
#include "control-source.inc"
static struct acpi_devnode node = { .ad_handle = VPU };
static char dma_tags[2];
static struct acpi_attach_args fixture_aa = { &node, NULL, &dma_tags[0], &dma_tags[1] };
static const struct cixvpu_resources description = {
    .mem = {{0x100000, 0x110000, 0x10000}, {0x300000, 0x320000, 0x10000}},
    .nmem = 2, .nirq = 1, .irq = 358, .irq_trigger = 0, .cca_present = 1, .cca = 0
};
static void fixture(void)
{
    assert(live == 0); diagnostic[0] = 0;
    clock_claim = (struct cixclk){0}; main_claim = (struct cixreset){0}; rcsu_claim = (struct cixreset){0};
    bad = faults = sta_count = nevents = delays = reads = maps = unmaps = 0; events[0] = 0;
    interrupt_context = power_live = clock_live = rc_asserted = false;
    native_poison = false; native_disable_calls = 0;
    cold = 1; profile_mode = false;
    attach_cca_status = AE_OK; attach_cca_value = 0;
    rate_status = 0; clock_rate = 600000000; id = 0x56621234; ncores = 2; nlsid = 4; svn = 0x12345678;
    memset(&scmi_cache, 0, sizeof(scmi_cache));
    scmi_info.info = (void *)scmi_cache.bytes;
    scmi_info.hid = (char *)(scmi_info.info + 1); scmi_info.uid = scmi_info.hid + 9;
    scmi_info.info->InfoSize = sizeof(scmi_cache.bytes); scmi_info.info->Type = ACPI_TYPE_DEVICE;
    scmi_info.info->Valid = ACPI_VALID_HID | ACPI_VALID_UID;
    memcpy(scmi_info.hid, "CIXHA006", 9); memcpy(scmi_info.uid, "0", 2);
    scmi_info.info->HardwareId.String = scmi_info.hid; scmi_info.info->HardwareId.Length = 9;
    scmi_info.info->UniqueId.String = scmi_info.uid; scmi_info.info->UniqueId.Length = 21;
    node.ad_root = ROOT; node.next = &scmi_node; node.ad_type = ACPI_TYPE_DEVICE;
    scmi_node = (struct acpi_devnode){ .ad_handle = SCMI, .ad_root = ROOT,
        .ad_devinfo = scmi_info.info, .ad_type = ACPI_TYPE_DEVICE };
    acpi_root.sc_head.first = &node; root_private = &acpi_root;
}
static void released(void)
{ CHECK(!clock_claim.claimed && !main_claim.claimed && !rcsu_claim.claimed); CHECK(!power_live && !clock_live && live == 0); }
static void metadata_tests(void)
{
    for (unsigned int i = 1; i <= 42; i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); bad = i;
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) != 0);
        CHECK(nevents == 0 && reads == 0 && maps == 0 && delays == 0 && sta_count == 0);
        released();
    }
    struct cixvpu_acpi_control c = {0}; struct cixvpu_resources invalid = description;
    fixture(); invalid.nmem = 1;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &invalid, true) == EINVAL);
    CHECK(nevents == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; interrupt_context = true;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == EWOULDBLOCK);
    CHECK(nevents == 0); interrupt_context = false;
}
static void query_tests(void)
{
    const unsigned int cases[] = {0, F_CLOCK_GET, F_RESET_GET, F_RCSU_GET, F_RATE};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        int rv = cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, false);
        CHECK(i == 0 ? rv == 0 : rv != 0);
        CHECK(reads == 0 && maps == 0 && delays == 0 && sta_count == 0);
        CHECK(strchr(events, 'S') == NULL && strchr(events, 'P') == NULL && strchr(events, 'A') == NULL && strchr(events, 'E') == NULL);
        if (i == 0) { CHECK(strcmp(events, "CMRGrmc") == 0); CHECK(c.report.rate == 600000000); }
        CHECK(c.report.rate_query_attempted == (cases[i] == 0 || cases[i] == F_RATE));
        CHECK(c.report.rate_valid == (cases[i] == 0));
        released();
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, false) == EALREADY);
    }
}
static void active_tests(void)
{
    struct cixvpu_acpi_control c = {0}; fixture();
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == 0);
    CHECK(strcmp(events, "CMRSPTEAdDdB0123UAKOtrmc") == 0);
    CHECK(c.report.id == 0x56621234 && c.report.ncores == 2 && c.report.nlsid == 4 && c.report.svn == 0x12345678);
    CHECK(c.report.rate == 0 && !c.report.rate_query_attempted && !c.report.rate_valid && c.report.error == 0 && c.report.cleanup_error == 0);
    CHECK(reads == 4 && delays == 2 && maps == 1 && unmaps == 1 && sta_count == 2);
    released();
    const uint32_t ids[] = {0x56500000, 0x56550000, 0x56600000, 0x56610000, 0x56620000, 0x56630000, 0x56640000, 0x56650000, 0};
    for (unsigned int i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) {
        fixture(); c = (struct cixvpu_acpi_control){0}; id = ids[i];
        CHECK((cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == 0) == (i < 7)); released();
    }
    for (unsigned int i = 0; i < 4; i++) {
        fixture(); c = (struct cixvpu_acpi_control){0};
        if (i < 2) ncores = i == 0 ? 0 : 5; else nlsid = i == 2 ? 0 : 5;
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) != 0); released();
    }
}
static void failures_tests(void)
{
    const unsigned int cases[] = {F_RCSU_STATE, F_POWER_ON, F_CLOCK_ON,
        F_RESET_DEASSERT, F_MAP, F_STA_ON, F_STA_ON_FAIL};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) != 0);
        CHECK(c.report.error != 0 && c.report.cleanup_error == 0);
        if (cases[i] == F_RCSU_STATE) CHECK(strchr(events, 'P') == NULL && strchr(events, 'A') == NULL);
        else { const char *a = strchr(events, 'A'), *off = strchr(events, 'O'); CHECK(a != NULL && off != NULL && a < off); CHECK(sta_count >= 1); }
        CHECK(reads == 0); released();
    }
    struct cixvpu_acpi_control c = {0}; fixture(); rc_asserted = true;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == EBUSY);
    CHECK(strcmp(events, "CMRSrmc") == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; clock_rate = 0;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == 0); CHECK(reads == 4); released();
}
static void rate_separation_tests(void)
{
    struct cixvpu_acpi_control c = {0};
    fixture(); rate_status = -4;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == 0);
    CHECK(strchr(events, 'G') == NULL);
    CHECK(strcmp(events, "CMRSPTEAdDdB0123UAKOtrmc") == 0);
    CHECK(c.report.rate == 0 && !c.report.rate_query_attempted && !c.report.rate_valid && c.report.identity_valid && c.report.released);
    released();
    fixture(); rate_status = -4; c = (struct cixvpu_acpi_control){0};
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, false) == EIO);
    CHECK(strcmp(events, "CMRGrmc") == 0);
    CHECK(c.report.rate == 0 && c.report.rate_query_attempted && !c.report.rate_valid && c.report.error == EIO && c.report.cleanup_error == 0);
    CHECK(reads == 0 && maps == 0 && sta_count == 0);
    released();
    fixture(); c = (struct cixvpu_acpi_control){0}; clock_rate = 0;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, false) == 0);
    CHECK(c.report.rate_query_attempted && c.report.rate_valid && c.report.rate == 0);
    released();
}
static void retention_tests(void)
{
    const unsigned int cases[] = {F_RESET_ASSERT, F_CLOCK_OFF, F_POWER_OFF, F_STA_OFF, F_STA_OFF_FAIL};
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct cixvpu_acpi_control c = {0}; fixture(); faults = cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) != 0);
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
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == EIO);
    CHECK(c.report.error == EIO && c.report.cleanup_error == EIO);
    CHECK(c.report.acpi_status == AE_ERROR && c.report.cleanup_acpi_status == AE_ERROR);
    faults = 0; CHECK(cixvpu_acpi_control_cleanup(&c) == 0); released();
    fixture(); c = (struct cixvpu_acpi_control){0}; faults = F_POWER_NO_LEASE;
    CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, true) == EIO);
    CHECK(c.report.error == EIO && c.report.cleanup_error == EIO);
    CHECK(c.report.acpi_status == AE_NOT_FOUND && c.report.cleanup_acpi_status == AE_ABORT_METHOD);
    CHECK(c.power_attempted && !power_live && clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
    CHECK(reads == 0 && !clock_live && strchr(events, 'E') == NULL);
    CHECK(cixvpu_acpi_control_cleanup(&c) == EIO); /* Remain conservative; do not invent a lease. */
    const unsigned int put_cases[] = {F_PUT_MAIN, F_PUT_RCSU, F_PUT_CLOCK};
    for (unsigned int i = 0; i < sizeof(put_cases)/sizeof(put_cases[0]); i++) {
        fixture(); c = (struct cixvpu_acpi_control){0}; faults = put_cases[i];
        CHECK(cixvpu_acpi_control_probe(&c, VPU, &fixture_aa, &description, false) != 0);
        CHECK(c.report.error == 0 && c.report.cleanup_error == EBUSY);
        CHECK(strchr(events, 'A') == NULL && strchr(events, 'P') == NULL);
        faults = 0; CHECK(cixvpu_acpi_control_cleanup(&c) == 0); released();
    }
}
#ifdef CIXVPU_ATTACH_API
/* The actual consumer uses modeled parser ownership and a deferred queue. */
#define __CTASSERT(x) _Static_assert(x, #x)
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL { NULL }
struct test_cfdata { unsigned int cf_flags; };
typedef struct test_cfdata *cfdata_t;
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void * const attach_refs[] = {(const void *)(m),(const void *)(a)}; \
    static const size_t attach_softc_size = (s)
#define aprint_naive(...) print_error(NULL, __VA_ARGS__)
#define aprint_normal(...) print_error(NULL, __VA_ARGS__)
#define aprint_normal_dev print_error
struct acpi_mem { uint64_t ar_base, ar_xbase, ar_length; };
struct acpi_irq { uint32_t ar_irq, ar_type; };
struct acpi_resources { unsigned int ar_nmem, ar_nirq, ar_nio, ar_niorange, ar_nmemrange, ar_ndrq; };
static int acpi_resource_parse_ops_quiet;
static struct test_cfdata attach_cf;
static void *attach_private;
static void (*deferred_callback)(device_t);
static device_t deferred_device;
static unsigned int parser_calls, cleanup_calls, defer_calls, raw_extra;
static unsigned int mountroot_calls;
static bool mountroot_immediate;
static ACPI_STATUS parser_status;
static struct cixvpu_resources attach_description;
static struct acpi_mem parser_memory[2];
static struct acpi_irq parser_irq;
static void *device_private(device_t d)
{ if (d == ROOT) return root_private; assert(d == VPU); return attach_private; }
static cfdata_t device_cfdata(device_t d) { assert(d == VPU); return &attach_cf; }
static int acpi_compatible_match(const void *aux, const struct device_compatible_entry *e)
{ (void)aux; assert(strcmp(e[0].compat, "CIXH3010") == 0); return 1; }
const char *AcpiFormatException(ACPI_STATUS status)
{ switch (status) { case AE_OK: return "AE_OK"; case AE_ERROR: return "AE_ERROR"; case AE_NOT_FOUND: return "AE_NOT_FOUND"; default: return "AE_OTHER"; } }
ACPI_STATUS AcpiWalkResources(ACPI_HANDLE h, const char *n, ACPI_WALK_RESOURCE_CALLBACK cb, void *arg)
{
    const unsigned int types[] = {10,10,15,7};
    assert(h == VPU && strcmp(n, "_CRS") == 0);
    for (unsigned int i = 0; i < 4; i++) {
        ACPI_RESOURCE resource = {0}; resource.Type = types[i];
        ACPI_STATUS rv = cb(&resource, arg); if (ACPI_FAILURE(rv)) return rv;
    }
    if (raw_extra) { ACPI_RESOURCE resource = {0}; resource.Type = raw_extra; return cb(&resource, arg); }
    return AE_OK;
}
static ACPI_STATUS acpi_resource_parse(device_t d, ACPI_HANDLE h, const char *name,
    struct acpi_resources *out, const void *ops)
{
    assert(d == VPU && h == VPU && strcmp(name, "_CRS") == 0 && ops == &acpi_resource_parse_ops_quiet);
    parser_calls++; memset(out, 0, sizeof(*out));
    out->ar_nmem = attach_description.nmem; out->ar_nirq = attach_description.nirq; out->ar_nio = attach_description.other;
    for (unsigned int i = 0; i < 2; i++) {
        parser_memory[i].ar_base = attach_description.mem[i].base;
        parser_memory[i].ar_xbase = attach_description.mem[i].translated;
        parser_memory[i].ar_length = attach_description.mem[i].length;
    }
    parser_irq.ar_irq = attach_description.irq; parser_irq.ar_type = attach_description.irq_trigger;
    return parser_status;
}
static const struct acpi_mem *acpi_res_mem(struct acpi_resources *r, unsigned int i)
{ return i < r->ar_nmem && i < 2 ? &parser_memory[i] : NULL; }
static const struct acpi_irq *acpi_res_irq(struct acpi_resources *r, unsigned int i)
{ return r->ar_nirq > i ? &parser_irq : NULL; }
static void acpi_resource_cleanup(struct acpi_resources *r)
{ cleanup_calls++; memset(r, 0, sizeof(*r)); memset(parser_memory, 0, sizeof(parser_memory)); memset(&parser_irq, 0, sizeof(parser_irq)); }
static void config_defer(device_t d, void (*cb)(device_t))
{ assert(d == VPU && cleanup_calls == 1); defer_calls++; deferred_device = d; deferred_callback = cb; }
static void config_mountroot(device_t d, void (*cb)(device_t))
{ assert(d == VPU && cleanup_calls == 1); mountroot_calls++; deferred_device = d; deferred_callback = cb; if (mountroot_immediate) cb(d); }
/* Firmware filesystem, SHA service and DMA API are orchestration boundaries.
 * Actual parser/planner run below. Actual allocator has its separate contract. */
typedef void *firmware_handle_t;
#define SHA256_DIGEST_LENGTH 32
typedef struct { unsigned int state; } SHA256_CTX;
static unsigned char fw_image[420736], dma_bytes[4096];
static const char selected_digest[] = "2f745394cc8700233448d74cf4fb14d18eba547d2259de5e0bbf825a1efef56d";
static off_t fw_size;
static unsigned int fw_fault, fw_opens, fw_closes, fw_reads, fw_hashes, fw_live;
static unsigned int dma_allocs, dma_views, dma_frees, dma_count;
static bool dma_live;
static unsigned int prepare_calls, prepared_frees;
static bool prepared_live;
static char fw_events[64];
static unsigned int fw_nevents;
static void fw_event(char c)
{ assert(fw_nevents + 1 < sizeof(fw_events)); fw_events[fw_nevents++] = c; fw_events[fw_nevents] = 0; }
static int firmware_open(const char *driver, const char *image, firmware_handle_t *out)
{ CHECK(strcmp(driver, "cixvpu") == 0 && strcmp(image, "hevcenc.fwb") == 0); fw_opens++; fw_event('o'); if (fw_fault == 1) return ENOENT; *out = fw_image; return 0; }
static off_t firmware_get_size(firmware_handle_t handle)
{ CHECK(handle == fw_image); fw_event('s'); return fw_size; }
static void *firmware_malloc(size_t n)
{ CHECK(n == (size_t)fw_size && fw_live == 0); fw_event('m'); if (fw_fault == 2) return NULL; void *p = malloc(n); assert(p); fw_live++; return p; }
static void firmware_free(void *p, size_t n)
{ CHECK(fw_live == 1 && n == (size_t)fw_size); fw_live--; fw_event('f'); free(p); }
static int firmware_read(firmware_handle_t handle, off_t offset, void *p, size_t n)
{ CHECK(handle == fw_image && offset == 0 && fw_live == 1 && n == (size_t)fw_size); fw_reads++; fw_event('r'); if (fw_fault == 3 || fw_fault == 13) return EIO; memcpy(p, fw_image, n); return 0; }
static int firmware_close(firmware_handle_t handle)
{ CHECK(handle == fw_image); fw_closes++; fw_event('c'); return fw_fault == 4 || fw_fault == 13 ? EBADF : 0; }
static int SHA256_Init(SHA256_CTX *ctx) { ctx->state = 1; return 1; }
static int SHA256_Update(SHA256_CTX *ctx, const uint8_t *p, size_t n)
{ CHECK(ctx->state == 1 && fw_live == 1 && fw_closes == 1 && n == (size_t)fw_size && memcmp(p, fw_image, n) == 0); ctx->state = 2; fw_hashes++; fw_event('h'); return 1; }
static int SHA256_Final(uint8_t out[32], SHA256_CTX *ctx)
{
    CHECK(ctx->state == 2); ctx->state = 3;
    for (unsigned int i = 0; i < 32; i++) {
        char byte[3] = {selected_digest[i * 2], selected_digest[i * 2 + 1], 0};
        out[i] = (uint8_t)strtoul(byte, NULL, 16);
    }
    if (fw_fault == 5) out[0] ^= 1;
    return 1;
}
#include "dma-header.inc"
struct cixvpu_dma { unsigned int count; struct cixvpu_fw_plan plan; };
static struct cixvpu_dma dma_owner;
int cixvpu_dma_alloc(bus_dma_tag_t dmat, bus_dma_tag_t dmat64,
    const struct cixvpu_fw_plan *plan, struct cixvpu_dma **out)
{
    CHECK(dmat == &dma_tags[0] && dmat64 == &dma_tags[1] && fw_live == (attach_cf.cf_flags == 5 ? 1U : 0U));
    CHECK(plan->cores == 1 && plan->backing_pages == 140 && *out == NULL);
    dma_allocs++; fw_event('a');
    if (fw_fault == 8) return ENOMEM;
    dma_owner.count = plan->backing_pages; dma_owner.plan = *plan; dma_count = dma_owner.count;
    dma_live = true; *out = &dma_owner; return 0;
}
int cixvpu_dma_page(struct cixvpu_dma *owner, unsigned int index,
    struct cixvpu_dma_page *out)
{
    CHECK(owner == &dma_owner && dma_live && index < owner->count);
    dma_views++;
    if (fw_fault == 9 && index == 1) return EIO;
    memset(dma_bytes, 0, sizeof(dma_bytes));
    if (fw_fault == 10 && index == 1) dma_bytes[4095] = 1;
    *out = (struct cixvpu_dma_page){ .kva = dma_bytes, .bus_address = 0x200000 };
    CHECK(cixvpu_fw_plan_page(&owner->plan, index, &out->plan_page) == 0);
    if (fw_fault == 12 && index == 1) out->plan_page.role = (enum cixvpu_fw_plan_role)UINT32_MAX;
    if (fw_fault == 14 && index == 1) out->plan_page.backing_index = UINT32_MAX;
    if (fw_fault == 15 && index == 1) out->kva = NULL;
    return 0;
}
int cixvpu_dma_free(struct cixvpu_dma **owner)
{
    CHECK(*owner == &dma_owner && dma_live); dma_frees++; fw_event('d');
    if (fw_fault == 11 || fw_fault == 19) return EIO;
    *owner = NULL; dma_live = false; return 0;
}
#include "prepare-header.inc"
/* Preparation is a service boundary here. No firmware copy/PTE emulation.
 * Its real implementation, allocator and MMU run in the separate D1 contract. */
struct cixvpu_prepared { struct cixvpu_dma *owner; };
static struct cixvpu_prepared prepared_owner;
int cixvpu_prepare_cpu(struct cixvpu_dma **owner, const struct cixvpu_fw_plan *plan,
    const void *bytes, size_t size, const uint8_t pin[32], struct cixvpu_prepared **out)
{
    CHECK(*owner == &dma_owner && dma_live && *out == NULL && !prepared_live);
    CHECK(fw_live == 1 && fw_closes == 1 && dma_views == 140);
    CHECK(bytes != NULL);
    if (bytes == NULL) return EINVAL;
    CHECK(size == sizeof(fw_image) && memcmp(bytes, fw_image, size) == 0);
    CHECK(plan->cores == 1 && plan->backing_pages == 140);
    for (unsigned int i = 0; i < 32; i++) {
        char byte[3] = {selected_digest[i * 2], selected_digest[i * 2 + 1], 0};
        CHECK(pin[i] == (uint8_t)strtoul(byte, NULL, 16));
    }
    prepare_calls++; fw_event('p');
    if (fw_fault == 16 || fw_fault == 19) return EIO;
    prepared_owner.owner = *owner; *owner = NULL; *out = &prepared_owner;
    prepared_live = true; return 0;
}
int cixvpu_prepared_free(struct cixvpu_prepared **owner)
{
    CHECK(*owner == &prepared_owner && prepared_live && dma_live && fw_live == 0);
    prepared_frees++; fw_event('q');
    if (fw_fault == 17) return EBUSY;
    prepared_live = false; dma_live = false; *owner = NULL; return 0;
}
#include "attachment-source.inc"
static union { max_align_t align; unsigned char bytes[4096]; } attachment_storage;
static void attach_fixture(unsigned int mode)
{
    fixture(); attach_cf.cf_flags = mode; attach_private = attachment_storage.bytes;
    memset(&attachment_storage, 0, sizeof(attachment_storage));
    deferred_callback = NULL; deferred_device = NULL; parser_calls = cleanup_calls = defer_calls = raw_extra = 0;
    mountroot_calls = 0;
    mountroot_immediate = false;
    parser_status = AE_OK; attach_description = description;
}
static void allocation_fixture(unsigned int fault)
{
    attach_fixture(4); fw_fault = fault; fw_size = sizeof(fw_image);
    fw_opens = fw_closes = fw_reads = fw_hashes = fw_live = 0;
    dma_allocs = dma_views = dma_frees = dma_count = 0;
    prepare_calls = prepared_frees = 0; prepared_live = false;
    dma_live = false; fw_nevents = 0; fw_events[0] = 0;
    memset(fw_image, 0, sizeof(fw_image));
    fw_image[4] = 5; fw_image[5] = 3;
    fw_image[97] = 0x10; /* text 4096 */
    fw_image[101] = 0x40; /* BSS at 16384 */
    fw_image[104] = 1;
    fw_image[108] = 1; /* one private BSS page */
    fw_image[173] = 0x40; /* empty shared extent at BSS start */
    if (fault == 6) fw_image[5] = 4;
    if (fault == 7) { fw_image[101] = 0x10; fw_image[173] = 0x10; }
}
static void allocation_run(void)
{
    struct acpi_attach_args temporary = fixture_aa;
    cixvpu_acpi_attach(NULL, VPU, &temporary);
    CHECK(mountroot_calls == 1 && defer_calls == 0 && deferred_callback != NULL);
    CHECK(fw_opens == 0 && dma_allocs == 0);
    temporary.aa_dmat = temporary.aa_dmat64 = OTHER;
    temporary.aa_node = NULL;
    if (deferred_callback) {
        deferred_callback(deferred_device);
        unsigned int opens = fw_opens, allocs = dma_allocs, frees = dma_frees;
        deferred_callback(deferred_device);
        CHECK(fw_opens == opens && dma_allocs == allocs && dma_frees == frees);
    }
    CHECK(nevents == 0 && maps == 0 && reads == 0 && sta_count == 0);
    CHECK(fw_live == 0);
}
static void allocation_tests(void)
{
    (void)firmware_open; (void)firmware_get_size; (void)firmware_malloc;
    (void)firmware_read; (void)firmware_close; (void)firmware_free; (void)SHA256_Init; (void)SHA256_Update; (void)SHA256_Final;
    (void)cixvpu_dma_alloc; (void)cixvpu_dma_page; (void)cixvpu_dma_free;
    allocation_fixture(0); allocation_run();
    CHECK(strcmp(fw_events, "osmrchfad") == 0);
    CHECK(dma_count == 140 && dma_views == 140 && dma_frees == 1 && !dma_live);
    CHECK(strstr(diagnostic, "one-core allocation fixture") != NULL);
    CHECK(strstr(diagnostic, "allocation role 0 pages 1") != NULL);
    CHECK(strstr(diagnostic, "allocation role 1 pages 1") != NULL);
    CHECK(strstr(diagnostic, "allocation role 10 pages 128") != NULL);
    CHECK(strstr(diagnostic, "allocation role 12 pages 2") != NULL);
    CHECK(strstr(diagnostic, "allocation result 0, primary 0 close 0 cleanup 0") != NULL);
    for (unsigned int fault = 1; fault <= 15; fault++) {
        allocation_fixture(fault); allocation_run();
        bool reached_dma = fault >= 8 && fault != 13;
        CHECK(dma_allocs == (reached_dma ? 1U : 0U));
        CHECK(dma_frees == (reached_dma && fault != 8 ? 1U : 0U));
        CHECK(fw_closes == (fault == 1 ? 0U : 1U));
        CHECK(fw_hashes == (fault >= 5 && fault != 13 ? 1U : 0U));
        CHECK(dma_live == (fault == 11));
        CHECK((((struct cixvpu_acpi_softc *)attach_private)->sc_dma != NULL) == (fault == 11));
        int primary = fault == 1 ? ENOENT : fault == 2 || fault == 8 ? ENOMEM :
            fault == 4 || fault == 11 ? 0 : fault >= 5 && fault <= 7 ? EINVAL : EIO;
        int close_result = fault == 4 || fault == 13 ? EBADF : 0;
        int cleanup_result = fault == 11 ? EIO : 0;
        char expected[128];
        snprintf(expected, sizeof(expected), "allocation result %d, primary %d close %d cleanup %d",
            primary ? primary : close_result ? close_result : cleanup_result,
            primary, close_result, cleanup_result);
        CHECK(strstr(diagnostic, expected) != NULL);
        if (fault >= 9 && fault != 13) CHECK(dma_views == (fault == 11 ? 140U : 2U));
    }
    const off_t sizes[] = {-1, 0, 179, 180, 1048576, 1048577, INT64_MAX};
    for (unsigned int i = 0; i < __arraycount(sizes); i++) {
        allocation_fixture(0); fw_size = sizes[i]; allocation_run();
        CHECK(fw_closes == 1 && fw_reads == 0 && fw_hashes == 0 && dma_allocs == 0);
    }
    allocation_fixture(0); mountroot_immediate = true;
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(mountroot_calls == 1 && dma_allocs == 1 && dma_frees == 1 && !dma_live);
    CHECK(strcmp(fw_events, "osmrchfad") == 0 && nevents == 0);
}
static void prepare_fixture(unsigned int fault)
{ allocation_fixture(fault); attach_cf.cf_flags = 5; }
static void prepare_run(void)
{
    allocation_run();
    if (deferred_callback) {
        unsigned int p = prepare_calls, q = prepared_frees;
        deferred_callback(deferred_device);
        CHECK(prepare_calls == p && prepared_frees == q);
    }
}
static void prepare_tests(void)
{
    (void)cixvpu_prepare_cpu; (void)cixvpu_prepared_free;
    prepare_fixture(0); prepare_run();
    CHECK(strcmp(fw_events, "osmrchapfq") == 0);
    CHECK(prepare_calls == 1 && prepared_frees == 1 && dma_frees == 0);
    CHECK(!dma_live && !prepared_live);
    CHECK(strstr(diagnostic, "one-core CPU prepare fixture") != NULL);
    CHECK(strstr(diagnostic, "prepare attempted yes, error 0") != NULL);
    CHECK(strstr(diagnostic, "retained DMA no prepared no") != NULL);
    CHECK(strstr(diagnostic, "prepare result 0, primary 0 close 0 prepared cleanup 0 DMA cleanup 0") != NULL);
    const unsigned int faults_to_test[] = {1,2,3,4,5,6,7,8,9,10,12,13,14,15,16,17,19};
    for (unsigned int i = 0; i < __arraycount(faults_to_test); i++) {
        unsigned int f = faults_to_test[i];
        prepare_fixture(f); prepare_run();
        bool reached_prepare = f >= 16;
        CHECK(prepare_calls == (reached_prepare ? 1U : 0U));
        CHECK(prepared_frees == (f == 17 ? 1U : 0U));
        bool reached_dma = f >= 8 && f != 13;
        CHECK(dma_allocs == (reached_dma ? 1U : 0U));
        CHECK(dma_frees == (reached_dma && f != 8 && f != 17 ? 1U : 0U));
        CHECK(fw_closes == (f == 1 ? 0U : 1U));
        CHECK(dma_live == (f == 17 || f == 19) && prepared_live == (f == 17));
        CHECK((((struct cixvpu_acpi_softc *)attach_private)->sc_dma != NULL) == (f == 19));
        int primary = f == 1 ? ENOENT : f == 2 || f == 8 ? ENOMEM :
            f == 4 || f == 17 ? 0 : f >= 5 && f <= 7 ? EINVAL : EIO;
        int close_result = f == 4 || f == 13 ? EBADF : 0;
        int prepare_result = f == 16 || f == 19 ? EIO : 0;
        int prepared_cleanup = f == 17 ? EBUSY : 0;
        int dma_cleanup = f == 19 ? EIO : 0;
        char expected[192];
        snprintf(expected, sizeof(expected), "prepare result %d, primary %d close %d prepared cleanup %d DMA cleanup %d",
            primary ? primary : close_result ? close_result : prepared_cleanup ? prepared_cleanup : dma_cleanup,
            primary, close_result, prepared_cleanup, dma_cleanup);
        CHECK(strstr(diagnostic, expected) != NULL);
        snprintf(expected, sizeof(expected), "prepare attempted %s, error %d", reached_prepare ? "yes" : "no", prepare_result);
        CHECK(strstr(diagnostic, expected) != NULL);
        snprintf(expected, sizeof(expected), "retained DMA %s prepared %s", f == 19 ? "yes" : "no", f == 17 ? "yes" : "no");
        CHECK(strstr(diagnostic, expected) != NULL);
    }
    prepare_fixture(0); mountroot_immediate = true;
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(mountroot_calls == 1 && prepare_calls == 1 && prepared_frees == 1);
    CHECK(strcmp(fw_events, "osmrchapfq") == 0 && !dma_live && !prepared_live && nevents == 0);
}
static void attach_tests(void)
{
    (void)device_private; (void)device_cfdata; (void)config_defer; (void)config_mountroot; (void)attach_refs;
    CHECK(attach_softc_size > 0 && attach_softc_size <= sizeof(attachment_storage));
    CHECK(cixvpu_acpi_match(NULL, NULL, &fixture_aa) != 0);
    attach_fixture(0); cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(parser_calls == 1 && cleanup_calls == 1 && defer_calls == 0);
    CHECK(nevents == 0 && reads == 0 && maps == 0); released();
    for (unsigned int mode = 1; mode <= 2; mode++) {
        struct acpi_attach_args temporary = fixture_aa;
        attach_fixture(mode); if (mode == 2) rate_status = -4; cixvpu_acpi_attach(NULL, VPU, &temporary);
        CHECK(parser_calls == 1 && cleanup_calls == 1 && defer_calls == 1 && deferred_callback != NULL);
        CHECK(nevents == 0 && reads == 0 && maps == 0);
        temporary.aa_node = NULL; temporary.aa_memt = OTHER;
        if (deferred_callback) {
            deferred_callback(deferred_device);
            CHECK(mode == 1 ? strcmp(events, "CMRGrmc") == 0 : strcmp(events, "CMRSPTEAdDdB0123UAKOtrmc") == 0);
            unsigned int before = nevents;
            deferred_callback(deferred_device); CHECK(nevents == before);
            CHECK(strstr(diagnostic, "AE_OK") != NULL);
            if (mode == 1) CHECK(strstr(diagnostic, "600000000") != NULL && strstr(diagnostic, "rate query attempted yes, rate valid yes") != NULL);
            else CHECK(strstr(diagnostic, "clock rate") == NULL && strstr(diagnostic, "rate query attempted no, rate valid no") != NULL);
            if (mode == 2) CHECK(strstr(diagnostic, "56621234") != NULL && strstr(diagnostic, "cores 2 slots 4") != NULL);
            else CHECK(strstr(diagnostic, "identity valid no, released yes") != NULL);
            released();
        }
    }
    attach_fixture(1); rate_status = -4; cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    if (deferred_callback) {
        deferred_callback(deferred_device);
        CHECK(strstr(diagnostic, "rate query attempted yes, rate valid no") != NULL);
        CHECK(strstr(diagnostic, "clock rate") == NULL && strstr(diagnostic, "released yes") != NULL);
        CHECK(reads == 0 && maps == 0 && sta_count == 0); released();
    } else CHECK(false);
    attach_fixture(4); cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(parser_calls == 1 && cleanup_calls == 1 && mountroot_calls == 1);
    CHECK(defer_calls == 0 && deferred_callback != NULL && nevents == 0 && maps == 0);
    attach_fixture(5); cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(parser_calls == 1 && cleanup_calls == 1 && mountroot_calls == 1);
    CHECK(defer_calls == 0 && deferred_callback != NULL && nevents == 0 && maps == 0);
    const unsigned int invalid_flags[] = {7, 0xffff, UINT32_MAX};
    for (unsigned int i = 0; i < sizeof(invalid_flags)/sizeof(invalid_flags[0]); i++) {
        attach_fixture(invalid_flags[i]); cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
        CHECK(parser_calls == 0 && defer_calls == 0 && nevents == 0);
    }
    for (unsigned int i = 0; i < 7; i++) {
        attach_fixture(2);
        switch (i) { case 0: raw_extra = 14; break; case 1: parser_status = AE_ERROR; break;
        case 2: attach_description.nmem = 1; break; case 3: attach_description.mem[1] = attach_description.mem[0]; break;
        case 4: attach_description.nirq = 0; break; case 5: attach_cca_status = AE_NOT_FOUND; break; case 6: attach_description.irq_trigger = 1; break; }
        cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
        CHECK(defer_calls == 0 && nevents == 0 && reads == 0 && maps == 0);
        CHECK(cleanup_calls == (i == 0 ? 0U : 1U));
    }
    attach_fixture(2); faults = F_POWER_ON | F_POWER_OFF;
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    if (deferred_callback) {
        deferred_callback(deferred_device);
        char expected[64]; snprintf(expected, sizeof(expected), "primary %d cleanup %d", EIO, EIO);
        CHECK(strstr(diagnostic, "AE_ERROR") != NULL && strstr(diagnostic, expected) != NULL && strstr(diagnostic, "released no") != NULL);
        CHECK(clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
    } else CHECK(false);
    attach_fixture(3);
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(parser_calls == 1 && cleanup_calls == 1 && defer_calls == 1);
    if (deferred_callback) {
        deferred_callback(deferred_device);
        CHECK(strcmp(events, "NMRSPTEAdDdB0123UAKOtrmc") == 0);
        CHECK(reads == 4 && delays == 2 && maps == 1 && unmaps == 1);
        CHECK(strstr(diagnostic, "native identity probe") != NULL);
        released();
    } else CHECK(false);
}
static void profile_fixture(void)
{
    attach_fixture(6); profile_mode = true;
    attach_description.mem[0] = (struct cixvpu_mem_resource){0x14230000,0x14230000,0x10000};
    attach_description.mem[1] = (struct cixvpu_mem_resource){0x14240000,0x14240000,0x10000};
    id = 0x56648002; ncores = nlsid = 4; svn = 0xe0c1afe1;
    profile_words[0] = 0x12345600; profile_words[1] = 0xabcdef01;
    profile_words[2] = 0x87651000; profile_words[3] = 0x11223344;
    profile_words[4] = 0x55667788; profile_words[5] = 0x99aabbcc;
    profile_words[6] = 0xddeeff00;
}
static void profile_run(void)
{
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(defer_calls == 1 && deferred_callback != NULL && mountroot_calls == 0);
    if (deferred_callback) {
        deferred_callback(deferred_device);
        unsigned int before = nevents;
        deferred_callback(deferred_device); CHECK(nevents == before);
    }
}
static void profile_checks(const struct cixvpu_acpi_control *c)
{
    const struct cixvpu_hw_profile *p = &c->profile;
    CHECK(p->valid && p->strap_pin2 == profile_words[0] && p->strap_pin0 == profile_words[1]);
    CHECK(p->pgctrl == profile_words[2] && p->repair_pass == profile_words[3]);
    CHECK(p->repair_enable == profile_words[4] && p->repair_busy == profile_words[5]);
    CHECK(p->clkforce == profile_words[6]);
    CHECK(p->harvesting_mask == ((~profile_words[0] >> 4) & 0xf));
    CHECK(p->usable_core_mask == p->harvesting_mask);
    CHECK(!c->report.rate_query_attempted && !c->report.rate_valid);
}
static void profile_preflight_tests(void)
{
    profile_fixture();
    struct cixvpu_acpi_control good = { .started = true, .mapped = true,
        .profile_mapped = true, .power_attempted = true, .clock_attempted = true,
        .clock = &clock_claim, .reset = &main_claim, .rcsu = &rcsu_claim,
        .span = 0x10000, .profile_span = 0x10000,
        .report = { .id = 0x56648002, .ncores = 4, .nlsid = 4,
            .svn = 0xe0c1afe1, .identity_valid = true }};
    struct cixvpu_hw_profile out, saved;
    memset(&out, 0xa5, sizeof(out)); memcpy(&saved, &out, sizeof(out));
    CHECK(cixvpu_profile_read(NULL, &out) == EINVAL);
    CHECK(cixvpu_profile_read(&good, NULL) == EINVAL);
    for (unsigned int i = 0; i < 18; i++) {
        struct cixvpu_acpi_control c = good;
        switch (i) {
        case 0: c.started = false; break;
        case 1: c.mapped = false; break;
        case 2: c.profile_mapped = false; break;
        case 3: c.power_attempted = false; break;
        case 4: c.clock_attempted = false; break;
        case 5: c.reset_safe = true; break;
        case 6: c.clock = NULL; break;
        case 7: c.reset = NULL; break;
        case 8: c.rcsu = NULL; break;
        case 9: c.report.id ^= 1; break;
        case 10: c.report.ncores = 0; break;
        case 11: c.report.nlsid = 5; break;
        case 12: c.report.svn ^= 1; break;
        case 13: c.profile_span = 0x30b; break;
        case 14: c.span = 0x27; break;
        case 15: c.report.identity_valid = false; break;
        case 16: c.power_released_fault = true; break;
        case 17: c.profile_span = 0; break;
        }
        CHECK(cixvpu_profile_read(&c, &out) == EINVAL);
        CHECK(memcmp(&out, &saved, sizeof(out)) == 0 && reads == 0 && nevents == 0);
    }
    interrupt_context = true;
    CHECK(cixvpu_profile_read(&good, &out) == EWOULDBLOCK);
    interrupt_context = false; cold = 0;
    CHECK(cixvpu_profile_read(&good, &out) == ENOTSUP);
    CHECK(memcmp(&out, &saved, sizeof(out)) == 0 && reads == 0);
    cold = 1;
    CHECK(!cixvpu_profile_bound(3, 0) && !cixvpu_profile_bound(8, 1));
    CHECK(!cixvpu_profile_bound(0x10000, UINT64_MAX));
    CHECK(!cixvpu_profile_bound(0x10000, 0x10000));
    CHECK(cixvpu_profile_bound(0x10000, 0xfffc));
}
static void profile_tests(void)
{
    profile_fixture(); profile_run();
    struct cixvpu_acpi_control *c = &((struct cixvpu_acpi_softc *)attach_private)->sc_control;
    CHECK(reads == 11 && maps == 2 && unmaps == 2 && delays == 2);
    CHECK(strcmp(events, "NMRSPTEAdDdB0123Vq456789qbFbXUAKOtrmc") == 0);
    CHECK(strstr(diagnostic, "native hardware profile") != NULL);
    CHECK(strstr(diagnostic, "profile valid yes") != NULL);
    CHECK(strstr(diagnostic, "STRAP_PIN2 0x12345600 STRAP_PIN0 0xabcdef01 PGCTRL 0x87651000") != NULL);
    CHECK(strstr(diagnostic, "repair raw PASS 0x11223344 ENBL 0x55667788 BUSY 0x99aabbcc") != NULL);
    CHECK(strstr(diagnostic, "CLKFORCE 0xddeeff00 harvesting 0xf usable core mask 0xf") != NULL);
    CHECK(!c->mapped && !c->profile_mapped); profile_checks(c); released();
    for (unsigned int mask = 0; mask < 16; mask++) {
        profile_fixture(); profile_words[0] = 0xffff000f | ((~mask & 0xf) << 4);
        profile_run(); c = &((struct cixvpu_acpi_softc *)attach_private)->sc_control;
        CHECK(c->profile.valid && c->profile.harvesting_mask == mask && c->profile.usable_core_mask == mask);
        CHECK(c->report.error == 0 && c->report.released && reads == 11); profile_checks(c); released();
    }
    for (unsigned int bad_profile = 0; bad_profile < 10; bad_profile++) {
        profile_fixture();
        switch (bad_profile) {
        case 0: attach_description.mem[0].base = attach_description.mem[0].translated = 0x14030000; break;
        case 1: attach_description.mem[0].translated += 0x400000; break;
        case 2: attach_description.mem[1].base = attach_description.mem[1].translated = 0x15040000; break;
        case 3: attach_description.mem[1].translated += 0x400000; break;
        case 4: attach_description.mem[1].length += 4; break;
        case 5: attach_cca_value = 1; break;
        case 6: attach_description.irq++; break;
        case 7: cold = 0; break;
        case 8: interrupt_context = true; break;
        case 9: cold = 2; break;
        }
        profile_run(); c = &((struct cixvpu_acpi_softc *)attach_private)->sc_control;
        CHECK(!c->profile.valid && reads == 0 && maps == 0 && nevents == 0);
        char result[64]; snprintf(result, sizeof(result), "probe result %d", bad_profile == 8 ? EWOULDBLOCK : ENOTSUP);
        CHECK(strstr(diagnostic, result) != NULL); released();
    }
    for (unsigned int bad_identity = 0; bad_identity < 4; bad_identity++) {
        profile_fixture();
        switch (bad_identity) { case 0: id ^= 1; break; case 1: ncores = 2; break;
        case 2: nlsid = 2; break; case 3: svn ^= 1; break; }
        profile_run(); c = &((struct cixvpu_acpi_softc *)attach_private)->sc_control;
        CHECK(c->report.error == ENOTSUP && !c->profile.valid && reads == 4 && maps == 1 && unmaps == 1);
        CHECK(strchr(events, 'V') == NULL && c->report.released); released();
    }
    const unsigned int cases[] = {F_CLOCK_GET,F_RESET_GET,F_RCSU_GET,F_RCSU_STATE,
        F_POWER_ON,F_STA_ON,F_STA_ON_FAIL,F_CLOCK_ON,F_RESET_ASSERT,
        F_RESET_DEASSERT,F_MAP,F_PROFILE_MAP,F_CLOCK_OFF,F_POWER_OFF,
        F_STA_OFF,F_STA_OFF_FAIL,F_NATIVE_POISON,F_PUT_MAIN,F_PUT_RCSU,F_PUT_CLOCK};
    for (unsigned int i = 0; i < __arraycount(cases); i++) {
        profile_fixture(); faults = cases[i]; profile_run();
        c = &((struct cixvpu_acpi_softc *)attach_private)->sc_control;
        bool snap = cases[i] == F_CLOCK_OFF || cases[i] == F_POWER_OFF ||
            cases[i] == F_STA_OFF || cases[i] == F_STA_OFF_FAIL ||
            cases[i] == F_PUT_MAIN || cases[i] == F_PUT_RCSU || cases[i] == F_PUT_CLOCK;
        CHECK(c->report.error != 0 || c->report.cleanup_error != 0);
        CHECK(c->profile.valid == snap && !c->mapped && !c->profile_mapped);
        CHECK(reads == (snap ? 11U : cases[i] == F_PROFILE_MAP ? 4U : 0U));
        CHECK(unmaps == (snap ? 2U : cases[i] == F_PROFILE_MAP ? 1U : 0U));
        if (snap) {
            profile_checks(c);
            CHECK(strstr(diagnostic, "profile valid yes") != NULL);
            CHECK(strchr(events, 'X') < strchr(events, 'U'));
            CHECK(strchr(events, 'U') < strrchr(events, 'A'));
        } else CHECK(strstr(diagnostic, "profile valid no") != NULL);
        if (cases[i] == F_NATIVE_POISON) {
            CHECK(reads == 0 && maps == 0 && power_live && native_disable_calls == 1);
            unsigned int before = nevents;
            CHECK(cixvpu_acpi_control_cleanup(c) == EIO && nevents == before);
            CHECK(clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed);
            continue;
        }
        if (cases[i] == F_STA_OFF || cases[i] == F_STA_OFF_FAIL) {
            CHECK(c->power_released_fault && !power_live && !c->report.released);
            continue;
        }
        faults = 0;
        if (!c->report.released) CHECK(cixvpu_acpi_control_cleanup(c) == 0);
        released();
    }
    profile_preflight_tests();
}
static struct cixvpu_acpi_report native_run(void)
{
    cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
    CHECK(deferred_callback != NULL);
    if (deferred_callback) deferred_callback(deferred_device);
    return ((struct cixvpu_acpi_softc *)attach_private)->sc_control.report;
}
static void native_tests(void)
{
    struct cixvpu_acpi_control control = {0};
    fixture();
    CHECK(cixvpu_acpi_control_probe_native(NULL, VPU, &fixture_aa, &description) == EINVAL);
    CHECK(cixvpu_acpi_control_probe_native(&control, VPU, NULL, &description) == EINVAL);
    CHECK(cixvpu_acpi_control_probe_native(&control, VPU, &fixture_aa, NULL) == EINVAL);
    interrupt_context = true;
    CHECK(cixvpu_acpi_control_probe_native(&control, VPU, &fixture_aa, &description) == EWOULDBLOCK);
    CHECK(!control.started && nevents == 0 && maps == 0);
    interrupt_context = false;
    for (unsigned int i = 0; i < 24; i++) {
        struct acpi_devnode duplicate;
        attach_fixture(3);
        switch (i) {
        case 0: node.ad_root = NULL; break;
        case 1: root_private = NULL; break;
        case 2: acpi_root.sc_head.first = &scmi_node; break;
        case 3: node.next = NULL; break;
        case 4: scmi_node.ad_root = OTHER; break;
        case 5: duplicate = scmi_node; duplicate.ad_handle = OTHER; scmi_node.next = &duplicate; break;
        case 6: scmi_node.ad_devinfo = NULL; break;
        case 7: scmi_node.ad_type = ACPI_TYPE_METHOD; break;
        case 8: scmi_info.info->Type = ACPI_TYPE_METHOD; break;
        case 9: scmi_info.info->InfoSize = sizeof(ACPI_DEVICE_INFO) - 1; break;
        case 10: scmi_info.info->Valid = ACPI_VALID_UID; break;
        case 11: scmi_info.info->Valid = ACPI_VALID_HID; break;
        case 12: scmi_info.hid[0] = 'X'; break;
        case 13: scmi_info.info->HardwareId.String = NULL; break;
        case 14: scmi_info.info->HardwareId.Length = 8; break;
        case 15: scmi_info.info->UniqueId.String = NULL; break;
        case 16: scmi_info.info->UniqueId.Length = 1; break;
        case 17: scmi_info.uid[0] = '1'; break;
        case 18: scmi_info.uid[1] = 'x'; break;
        case 19: scmi_info.info->UniqueId.Length = UINT32_MAX; break;
        case 20: scmi_info.info->HardwareId.String = (char *)scmi_info.info + sizeof(scmi_cache.bytes) - 1; break;
        case 21: node.ad_type = ACPI_TYPE_METHOD; break;
        case 22: scmi_node.ad_handle = NULL; break;
        default: scmi_info.info->UniqueId.String = (char *)scmi_info.info + sizeof(scmi_cache.bytes) - 1; break;
        }
        struct cixvpu_acpi_report r = native_run();
        CHECK(r.error != 0 && r.released && !r.identity_valid);
        CHECK(nevents == 0 && maps == 0 && reads == 0 && sta_count == 0);
        released();
    }
    for (unsigned int i = 0; i < 2; i++) {
        attach_fixture(3);
        scmi_info.info->UniqueId.Length = i == 0 ? 2 : 21;
        memset(scmi_info.uid + 2, 0xa5, 19);
        struct cixvpu_acpi_report r = native_run();
        CHECK(r.error == 0 && r.cleanup_error == 0 && r.identity_valid && r.released);
        CHECK(strcmp(events, "NMRSPTEAdDdB0123UAKOtrmc") == 0);
        CHECK(!r.rate_query_attempted && !r.rate_valid);
        unsigned int before = nevents;
        if (deferred_callback) deferred_callback(deferred_device);
        CHECK(nevents == before);
        released();
    }
    for (unsigned int i = 1; i <= 42; i++) {
        attach_fixture(3); bad = i;
        struct cixvpu_acpi_report r = native_run();
        CHECK(r.error != 0 && r.cleanup_error == 0 && r.released);
        CHECK(nevents == 0 && reads == 0 && maps == 0 && sta_count == 0);
        released();
    }
    const uint32_t ids[] = {0x56500000, 0x56550000, 0x56600000, 0x56610000,
        0x56620000, 0x56630000, 0x56640000, 0x56650000, 0};
    for (unsigned int i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) {
        attach_fixture(3); id = ids[i];
        struct cixvpu_acpi_report r = native_run();
        CHECK(r.identity_valid == (i < 7) && r.error == (i < 7 ? 0 : ENODEV));
        CHECK(r.released && r.cleanup_error == 0 && reads == 4);
        released();
    }
    const unsigned int cases[] = { F_CLOCK_GET, F_RESET_GET, F_RCSU_GET,
        F_RCSU_STATE, F_POWER_ON, F_STA_ON, F_STA_ON_FAIL, F_CLOCK_ON,
        F_RESET_DEASSERT, F_MAP, F_CLOCK_OFF, F_NATIVE_POISON };
    for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        attach_fixture(3); faults = cases[i];
        struct cixvpu_acpi_report r = native_run();
        CHECK(r.error != 0 || r.cleanup_error != 0);
        CHECK(strchr(events, 'C') == NULL && strchr(events, 'G') == NULL);
        if (cases[i] == F_CLOCK_ON || cases[i] == F_NATIVE_POISON) CHECK(maps == 0 && reads == 0);
        if (cases[i] == F_NATIVE_POISON) {
            CHECK(r.error == EIO && r.cleanup_error == EIO && !r.released);
            CHECK(strcmp(events, "NMRSPTEA") == 0 && native_disable_calls == 1);
            CHECK(clock_claim.claimed && main_claim.claimed && rcsu_claim.claimed && power_live);
            unsigned int before = nevents;
            CHECK(cixvpu_acpi_control_cleanup(&((struct cixvpu_acpi_softc *)attach_private)->sc_control) == EIO);
            CHECK(nevents == before && native_disable_calls == 2);
        } else if (cases[i] == F_CLOCK_OFF) {
            CHECK(!r.released && power_live && clock_claim.claimed);
        } else released();
    }
    for (unsigned int i = 0; i < 7; i++) {
        attach_fixture(3);
        switch (i) { case 0: raw_extra = 14; break; case 1: parser_status = AE_ERROR; break;
        case 2: attach_description.nmem = 1; break; case 3: attach_description.mem[1] = attach_description.mem[0]; break;
        case 4: attach_description.nirq = 0; break; case 5: attach_cca_status = AE_NOT_FOUND; break; case 6: attach_description.irq_trigger = 1; break; }
        cixvpu_acpi_attach(NULL, VPU, &fixture_aa);
        CHECK(defer_calls == 0 && nevents == 0 && reads == 0 && maps == 0);
        CHECK(cleanup_calls == (i == 0 ? 0U : 1U));
    }
}
#endif
#endif
int main(void)
{
#ifdef CIXVPU_CONTROL_API
    (void)cixclk_acpi_get_native;
    metadata_tests(); query_tests(); active_tests(); failures_tests(); rate_separation_tests(); retention_tests();
#ifdef CIXVPU_ATTACH_API
    attach_tests(); native_tests(); allocation_tests(); prepare_tests(); profile_tests();
#endif
    CHECK(live == 0 && allocations != 0);
#else
    CHECK(!"native VPU control helper is absent");
#endif
    printf("cixvpu control actual-source contract: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
