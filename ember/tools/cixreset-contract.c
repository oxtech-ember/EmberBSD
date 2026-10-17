/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: original actual-source CIX reset contracts, AI-assisted. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>

static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

#ifdef CIXRESET_DRIVER_API
/* Only OS services are modeled. Resources use the fork's public ACPICA types.
 * These fixtures do not test AML decoding or physical bus transactions. */
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *device_t;
typedef void *cfdata_t;
typedef pthread_mutex_t kmutex_t;
typedef unsigned int once_t;
#define ONCE_DECL(x) once_t x
#define RUN_ONCE(o,f) (*(o) ? 0 : ((*(o) = 1), (f)()))
#define __BIT(n) (UINT32_C(1) << (n))
#define __arraycount(x) (sizeof(x) / sizeof((x)[0]))
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL { NULL }
struct acpi_devnode { ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void * const test_attach_refs[] = {(const void *)(m), (const void *)(a)}
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
static unsigned int errors;
static void print_error(device_t d, const char *fmt, ...)
{ (void)d; (void)fmt; errors++; }
#define aprint_error_dev print_error
static char provider_token, wrong_token;
#define HANDLE ((void *)&provider_token)
#define WRONG_HANDLE ((void *)&wrong_token)
static ACPI_RESOURCE resources[4];
static unsigned int nresources;
static ACPI_INTEGER uid, cca;
static bool missing_uid, missing_cca, compatible = true, map_fail, dropped_write;
static bool hard_intr, soft_intr, walk_fail;
static unsigned int maps, unmaps, reads, writes, barriers, held;
static uint32_t registers[0x1000 / 4];
static void *private_data;
static void mutex_init(kmutex_t *m, int a, int b)
{ (void)a; (void)b; assert(pthread_mutex_init(m, NULL) == 0); }
static void mutex_enter(kmutex_t *m)
{ assert(pthread_mutex_lock(m) == 0); assert(held == 0); held = 1; }
static void mutex_exit(kmutex_t *m)
{ assert(held == 1); held = 0; assert(pthread_mutex_unlock(m) == 0); }
static bool cpu_intr_p(void) { return hard_intr; }
static bool cpu_softintr_p(void) { return soft_intr; }
static void *device_private(device_t d) { (void)d; return private_data; }
static int acpi_compatible_match(const void *a, const struct device_compatible_entry *e)
{ (void)a; assert(strcmp(e[0].compat, "CIXHA020") == 0); return compatible; }
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h, const char *n, ACPI_INTEGER *v)
{
    assert(h == HANDLE);
    if (strcmp(n, "_UID") == 0) {
        if (missing_uid) return AE_NOT_FOUND;
        *v = uid;
    } else {
        assert(strcmp(n, "_CCA") == 0);
        if (missing_cca) return AE_NOT_FOUND;
        *v = cca;
    }
    return AE_OK;
}
ACPI_STATUS AcpiWalkResources(ACPI_HANDLE h, const char *n,
    ACPI_WALK_RESOURCE_CALLBACK f, void *p)
{
    assert(h == HANDLE && strcmp(n, "_CRS") == 0);
    if (walk_fail) return AE_ERROR;
    for (unsigned int i = 0; i < nresources; i++) {
        ACPI_STATUS rv = f(&resources[i], p);
        if (ACPI_FAILURE(rv)) return rv;
    }
    return AE_OK;
}
static int bus_space_map(bus_space_tag_t t, bus_addr_t a, bus_size_t n,
    int f, bus_space_handle_t *h)
{
    (void)t;
    assert(a == 0x16000000 && n == 0x1000 && f == 0);
    maps++;
    if (map_fail) return ENOMEM;
    *h = 1;
    return 0;
}
static void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t n)
{ (void)t; assert(h == 1 && n == 0x1000); unmaps++; }
static uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    assert(held && h == 1 && (o == 0x400 || o == 0x804));
    reads++;
    return registers[o / 4];
}
static void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t o, uint32_t v)
{
    (void)t;
    assert(held && h == 1 && (o == 0x400 || o == 0x804));
    writes++;
    if (!dropped_write) registers[o / 4] = v;
}
static void bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t o, bus_size_t n, int flags)
{
    (void)t;
    assert(held && h == 1 && (o == 0x400 || o == 0x804) && n == 4);
    assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
    barriers++;
}
#include "cixreset-header.inc"
#include "cixreset-source.inc"
static struct cixreset_acpi_softc sc, duplicate_sc;
static struct acpi_devnode node = { HANDLE };
static struct acpi_attach_args aa = { &node, NULL };
static void fixture(void)
{
    memset(resources, 0, sizeof(resources));
    resources[0].Type = ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
    resources[0].Data.FixedMemory32.WriteProtect = ACPI_READ_WRITE_MEMORY;
    resources[0].Data.FixedMemory32.Address = 0x16000000;
    resources[0].Data.FixedMemory32.AddressLength = 0x1000;
    resources[1].Type = ACPI_RESOURCE_TYPE_END_TAG;
    nresources = 2;
    uid = cca = 0;
    missing_uid = missing_cca = walk_fail = false;
    compatible = true;
    map_fail = dropped_write = hard_intr = soft_intr = false;
    maps = unmaps = reads = writes = barriers = errors = 0;
    private_data = &sc;
}
static void rejected_profile(void)
{
    CHECK(cixreset_acpi_match(NULL, NULL, &aa) == 0);
    cixreset_acpi_attach(NULL, &sc, &aa);
    CHECK(maps == 0 && reads == 0 && writes == 0);
}
static void profile_tests(void)
{
    fixture(); uid = 1; rejected_profile();
    fixture(); cca = 1; rejected_profile();
    fixture(); missing_uid = true; rejected_profile();
    fixture(); missing_cca = true; rejected_profile();
    fixture(); compatible = false; rejected_profile();
    fixture(); walk_fail = true; rejected_profile();
    fixture(); resources[0].Data.FixedMemory32.Address++; rejected_profile();
    fixture(); resources[0].Data.FixedMemory32.AddressLength--; rejected_profile();
    fixture(); resources[0].Data.FixedMemory32.WriteProtect = ACPI_READ_ONLY_MEMORY; rejected_profile();
    fixture(); resources[0].Type = ACPI_RESOURCE_TYPE_MEMORY32; rejected_profile();
    fixture(); resources[1] = resources[0]; nresources = 3;
    resources[2].Type = ACPI_RESOURCE_TYPE_END_TAG; rejected_profile();
    fixture(); resources[2] = resources[1]; nresources = 3; rejected_profile();
    fixture(); nresources = 1; rejected_profile();
    fixture(); resources[0].Type = ACPI_RESOURCE_TYPE_END_TAG; rejected_profile();
    fixture(); resources[1].Type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ; rejected_profile();
    fixture(); map_fail = true;
    cixreset_acpi_attach(NULL, &sc, &aa);
    CHECK(maps == 1 && unmaps == 0 && reads == 0 && writes == 0);
}
static void claim_tests(void)
{
    struct cixreset *r = (void *)&wrong_token, *r2 = NULL;
    fixture();
    CHECK(cixreset_acpi_get(HANDLE, 14, &r) == ENXIO);
    CHECK(r == (void *)&wrong_token);
    CHECK(cixreset_acpi_match(NULL, NULL, &aa) != 0);
    cixreset_acpi_attach(NULL, &sc, &aa);
    CHECK(maps == 1 && reads == 0 && writes == 0 && barriers == 0);
    private_data = &duplicate_sc;
    cixreset_acpi_attach(NULL, &duplicate_sc, &aa);
    CHECK(maps == 2 && unmaps == 1 && reads == 0 && writes == 0);
    CHECK(cixreset_acpi_get(WRONG_HANDLE, 14, &r) == ENXIO);
    CHECK(r == (void *)&wrong_token);
    CHECK(cixreset_acpi_get(HANDLE, 0, &r) == EINVAL);
    CHECK(cixreset_acpi_get(HANDLE, UINT32_MAX, &r) == EINVAL);
    CHECK(cixreset_acpi_get(NULL, 14, &r) == EINVAL);
    CHECK(cixreset_acpi_get(HANDLE, 14, NULL) == EINVAL);
    CHECK(r == (void *)&wrong_token);
    CHECK(cixreset_acpi_get(HANDLE, 14, &r) == 0);
    CHECK(cixreset_acpi_get(HANDLE, 14, &r2) == EBUSY && r2 == NULL);
    CHECK(cixreset_acpi_get(HANDLE, 142, &r2) == 0 && r2 != r);
    CHECK(reads == 0 && writes == 0);
    CHECK(cixreset_acpi_put(r) == 0); /* Untouched claim. */
    CHECK(cixreset_acpi_assert(r) == EINVAL);
    CHECK(cixreset_acpi_put(r2) == 0);
    CHECK(cixreset_acpi_put(NULL) == EINVAL);
    CHECK(cixreset_acpi_deassert((void *)&wrong_token) == EINVAL);
    hard_intr = true;
    CHECK(cixreset_acpi_get(HANDLE, 14, &r) == EWOULDBLOCK);
    CHECK(cixreset_acpi_assert(r) == EWOULDBLOCK);
    hard_intr = false; soft_intr = true;
    CHECK(cixreset_acpi_put(r) == EWOULDBLOCK);
    soft_intr = false;
    CHECK(reads == 0 && writes == 0);
}
static void operation_tests(void)
{
    const uint32_t ids[] = { 14, 142 };
    const unsigned int offsets[] = { 0x400 / 4, 0x804 / 4 };
    const uint32_t bits[] = { 0x800, 0x80000 };
    struct cixreset *r = NULL;
    for (unsigned int j = 0; j < 2; j++) {
        CHECK(cixreset_acpi_get(HANDLE, ids[j], &r) == 0);
        /* Every neighbor bit separately, then all bits: detect clobbered RMW. */
        for (unsigned int i = 0; i <= 32; i++) {
            uint32_t before = i == 32 ? UINT32_MAX : (UINT32_C(1) << i);
            registers[offsets[j]] = before;
            CHECK(cixreset_acpi_assert(r) == 0);
            CHECK(registers[offsets[j]] == (before & ~bits[j]));
            CHECK(cixreset_acpi_deassert(r) == 0);
            CHECK(registers[offsets[j]] == (before | bits[j]));
        }
        CHECK(cixreset_acpi_put(r) == EBUSY);
        CHECK(cixreset_acpi_assert(r) == 0);
        CHECK(cixreset_acpi_put(r) == 0);
        CHECK(cixreset_acpi_get(HANDLE, ids[j], &r) == 0);
        registers[offsets[j]] = UINT32_MAX;
        dropped_write = true;
        CHECK(cixreset_acpi_assert(r) == EIO);
        CHECK(cixreset_acpi_put(r) == EBUSY);
        struct cixreset *other = NULL;
        CHECK(cixreset_acpi_get(HANDLE, ids[j], &other) == EBUSY && other == NULL);
        dropped_write = false;
        CHECK(cixreset_acpi_assert(r) == 0); /* Recover while ownership retained. */
        dropped_write = true;
        CHECK(cixreset_acpi_deassert(r) == EIO);
        CHECK(cixreset_acpi_put(r) == EBUSY);
        dropped_write = false;
        CHECK(cixreset_acpi_assert(r) == 0);
        CHECK(cixreset_acpi_put(r) == 0);
    }
    CHECK(reads == 2 * writes && barriers == writes);
    CHECK(errors >= 4);
}
static void state_tests(void)
{
#ifdef CIXRESET_STATE_API
    struct cixreset *r = NULL;
    bool asserted = true;
    unsigned int before_reads = reads, before_writes = writes, before_barriers = barriers;
    CHECK(cixreset_acpi_get_state(NULL, &asserted) == EINVAL && asserted);
    CHECK(cixreset_acpi_get_state((void *)&wrong_token, &asserted) == EINVAL && asserted);
    CHECK(reads == before_reads && writes == before_writes && barriers == before_barriers);
    CHECK(cixreset_acpi_get(HANDLE, 14, &r) == 0);
    CHECK(cixreset_acpi_get_state(r, NULL) == EINVAL);
    hard_intr = true;
    CHECK(cixreset_acpi_get_state(r, &asserted) == EWOULDBLOCK && asserted);
    hard_intr = false; soft_intr = true;
    CHECK(cixreset_acpi_get_state(r, &asserted) == EWOULDBLOCK && asserted);
    soft_intr = false;
    CHECK(reads == before_reads && writes == before_writes && barriers == before_barriers);
    registers[0x400 / 4] = 0x800;
    CHECK(cixreset_acpi_get_state(r, &asserted) == 0 && !asserted);
    CHECK(reads == before_reads + 1 && writes == before_writes && barriers == before_barriers);
    CHECK(cixreset_acpi_put(r) == 0); /* Read did not change untouched state. */
    CHECK(cixreset_acpi_get(HANDLE, 142, &r) == 0);
    registers[0x804 / 4] = 0xfff7ffff;
    CHECK(cixreset_acpi_get_state(r, &asserted) == 0 && asserted);
    CHECK(reads == before_reads + 2 && writes == before_writes && barriers == before_barriers);
    CHECK(cixreset_acpi_put(r) == 0);
    CHECK(cixreset_acpi_get(HANDLE, 14, &r) == 0);
    registers[0x400 / 4] = 0x800;
    dropped_write = true;
    CHECK(cixreset_acpi_assert(r) == EIO);
    before_reads = reads; before_writes = writes; before_barriers = barriers;
    CHECK(cixreset_acpi_get_state(r, &asserted) == 0 && !asserted);
    CHECK(reads == before_reads + 1 && writes == before_writes && barriers == before_barriers);
    CHECK(cixreset_acpi_put(r) == EBUSY); /* Read must not recover uncertain state. */
    registers[0x400 / 4] = 0;
    CHECK(cixreset_acpi_get_state(r, &asserted) == 0 && asserted);
    CHECK(cixreset_acpi_put(r) == EBUSY); /* Even an asserted read does not recover. */
    dropped_write = false;
    CHECK(cixreset_acpi_assert(r) == 0);
    CHECK(cixreset_acpi_get_state(r, &asserted) == 0 && asserted);
    CHECK(cixreset_acpi_put(r) == 0);
#else
    CHECK(!"read-only owned reset state API is absent");
#endif
}
static void *worker(void *arg)
{
    struct cixreset *r = arg;
    for (unsigned int i = 0; i < 2000; i++) {
        assert(cixreset_acpi_deassert(r) == 0);
        assert(cixreset_acpi_assert(r) == 0);
    }
    return NULL;
}
static void concurrent_tests(void)
{
    struct cixreset *a = NULL, *b = NULL;
    pthread_t t[2];
    CHECK(cixreset_acpi_get(HANDLE, 14, &a) == 0);
    CHECK(cixreset_acpi_get(HANDLE, 142, &b) == 0);
    registers[0x400 / 4] = 0x5a5a5a5a;
    registers[0x804 / 4] = 0xa5a5a5a5;
    CHECK(pthread_create(&t[0], NULL, worker, a) == 0);
    CHECK(pthread_create(&t[1], NULL, worker, b) == 0);
    CHECK(pthread_join(t[0], NULL) == 0);
    CHECK(pthread_join(t[1], NULL) == 0);
    CHECK(registers[0x400 / 4] == 0x5a5a525a);
    CHECK(registers[0x804 / 4] == 0xa5a5a5a5);
    CHECK(cixreset_acpi_put(a) == 0 && cixreset_acpi_put(b) == 0);
    CHECK(reads == 2 * writes && barriers == writes);
}
#endif
int main(void)
{
#ifdef CIXRESET_DRIVER_API
    (void)test_attach_refs;
    profile_tests(); claim_tests(); operation_tests(); concurrent_tests(); state_tests();
#else
    CHECK(!"native CIX reset provider is absent");
#endif
    printf("cixreset actual-source contract: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
