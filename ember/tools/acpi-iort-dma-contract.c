/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: original actual-source ACPI DMA contracts, AI-assisted. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define le16toh(x) OSSwapLittleToHostInt16(x)
#else
#include <endian.h>
#endif
#define ACPI_APPLICATION
#include <acpi.h>

/* The runner extracts native function bytes verbatim. ACPICA table structures
 * are real. Namespace, resource parsing and kernel allocation are modeled.
 * This does not qualify physical DMA routing or malformed IORT handling. */
static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
#define KM_SLEEP 0
#define _BUS_DMAMAP_COHERENT 0x10000
#define __BITS(h,l) ((UINT64_MAX >> (63 - (h))) & (UINT64_MAX << (l)))

struct arm32_dma_range {
    uintptr_t dr_sysbase, dr_busbase, dr_len;
    uint32_t dr_flags;
};
struct arm32_bus_dma_tag {
    int _nranges;
    struct arm32_dma_range *_ranges;
};
typedef struct arm32_bus_dma_tag *bus_dma_tag_t;
struct acpi_devnode {
    struct acpi_devnode *ad_parent;
    ACPI_DEVICE_INFO *ad_devinfo;
    ACPI_HANDLE ad_handle;
    bus_dma_tag_t ad_dmat64;
};
struct acpi_softc { void *sc_dev; };
struct acpi_mem { uintptr_t ar_base, ar_xbase, ar_length; };
struct acpi_resources {
    int ar_nmem;
    struct acpi_mem *mem;
};
static struct arm32_bus_dma_tag arm_generic_dma_tag;
static const int acpi_resource_parse_ops_quiet;
static unsigned int live, allocations, cleanups, parses, table_reads, errors;
static struct { void *ptr; size_t size; } owned[16];
static _Alignas(max_align_t) unsigned char table[512];
static size_t table_size;
static ACPI_STATUS table_status, parse_status;
static int parse_count;
static struct acpi_mem parse_mem[2];
static char vpu_token, other_token, module_token, ancestor_token;
#define VPU_HANDLE ((ACPI_HANDLE)&vpu_token)
#define OTHER_HANDLE ((ACPI_HANDLE)&other_token)
#define MODULE_HANDLE ((ACPI_HANDLE)&module_token)
#define ANCESTOR_HANDLE ((ACPI_HANDLE)&ancestor_token)
static bool module_dma, ancestor_dma, vpu_cca_present, ancestor_cca_present;
static ACPI_INTEGER vpu_cca, ancestor_cca;
static void *kmem_alloc(size_t size, int flags)
{
    assert(flags == KM_SLEEP && size > 0 && live < 16);
    void *p = malloc(size);
    assert(p != NULL); /* Native KM_SLEEP does not return an allocation error. */
    for (unsigned int i = 0; i < 16; i++) {
        if (owned[i].ptr == NULL) {
            owned[i].ptr = p;
            owned[i].size = size;
            live++;
            allocations++;
            return p;
        }
    }
    abort();
}
static void *kmem_zalloc(size_t size, int flags)
{
    void *p = kmem_alloc(size, flags);
    memset(p, 0, size);
    return p;
}
static void kmem_free(void *p, size_t size)
{
    for (unsigned int i = 0; i < 16; i++) {
        if (owned[i].ptr == p) {
            assert(p != NULL && owned[i].size == size && live > 0);
            owned[i].ptr = NULL;
            owned[i].size = 0;
            live--;
            free(p);
            return;
        }
    }
    abort();
}
ACPI_STATUS AcpiGetTable(const char *signature, UINT32 instance, ACPI_TABLE_HEADER **out)
{
    assert(strcmp(signature, ACPI_SIG_IORT) == 0 && instance == 0);
    table_reads++;
    if (ACPI_FAILURE(table_status)) return table_status;
    *out = (ACPI_TABLE_HEADER *)table;
    return AE_OK;
}
ACPI_STATUS AcpiGetHandle(ACPI_HANDLE parent, const char *name, ACPI_HANDLE *out)
{
    if (parent == NULL) {
        if (strcmp(name, "\\_SB_.VPU0") == 0) *out = VPU_HANDLE;
        else if (strcmp(name, "\\_SB_.OTHR") == 0) *out = OTHER_HANDLE;
        else return AE_NOT_FOUND;
        return AE_OK;
    }
    assert(strcmp(name, "_DMA") == 0);
    if ((parent == MODULE_HANDLE && module_dma) ||
        (parent == ANCESTOR_HANDLE && ancestor_dma)) {
        *out = parent;
        return AE_OK;
    }
    return AE_NOT_FOUND;
}
static bool acpi_match_hid(const ACPI_DEVICE_INFO *info, const char * const *ids)
{
    assert(strcmp(ids[0], "ACPI0004") == 0 && ids[1] == NULL);
    return info->HardwareId.String != NULL &&
        strcmp(info->HardwareId.String, "ACPI0004") == 0;
}
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h, const char *name, ACPI_INTEGER *v)
{
    assert(strcmp(name, "_CCA") == 0);
    if (h == VPU_HANDLE && vpu_cca_present) *v = vpu_cca;
    else if (h == ANCESTOR_HANDLE && ancestor_cca_present) *v = ancestor_cca;
    else return AE_NOT_FOUND;
    return AE_OK;
}
static ACPI_STATUS acpi_resource_parse_any(void *dev, ACPI_HANDLE h,
    const char *name, struct acpi_resources *res, const int *ops)
{
    (void)dev;
    assert((h == MODULE_HANDLE && module_dma) ||
        (h == ANCESTOR_HANDLE && ancestor_dma));
    assert(strcmp(name, "_DMA") == 0 && ops == &acpi_resource_parse_ops_quiet);
    parses++;
    memset(res, 0, sizeof(*res));
    if (ACPI_FAILURE(parse_status)) return parse_status;
    res->ar_nmem = parse_count;
    if (parse_count != 0) {
        res->mem = kmem_alloc((size_t)parse_count * sizeof(*res->mem), KM_SLEEP);
        memcpy(res->mem, parse_mem, (size_t)parse_count * sizeof(*res->mem));
    }
    return AE_OK;
}
static struct acpi_mem *acpi_res_mem(struct acpi_resources *res, int n)
{
    assert(n >= 0 && n < res->ar_nmem);
    return &res->mem[n];
}
static void acpi_resource_cleanup(struct acpi_resources *res)
{
    if (res->mem != NULL)
        kmem_free(res->mem, (size_t)res->ar_nmem * sizeof(*res->mem));
    memset(res, 0, sizeof(*res));
    cleanups++;
}
static const char *acpi_name(ACPI_HANDLE h)
{ (void)h; return "test"; }
const char *AcpiFormatException(ACPI_STATUS rv)
{ (void)rv; return "modeled ACPI error"; }
static void print_error(void *dev, const char *fmt, ...)
{ (void)dev; (void)fmt; errors++; }
static void print_debug(void *dev, const char *fmt, ...)
{ (void)dev; (void)fmt; }
#define aprint_error_dev print_error
#define aprint_debug_dev print_debug

#include "acpi-iort-native.inc"
#include "acpi-dma-native.inc"

static ACPI_DEVICE_INFO vpu_info, module_info, ancestor_info;
static struct acpi_devnode vpu, module, ancestor;
static struct acpi_softc softc;
static void add_node(unsigned int type, const char *name, unsigned int limit)
{
    size_t size = sizeof(ACPI_IORT_NODE);
    if (name != NULL) size += sizeof(ACPI_IORT_NAMED_COMPONENT) + strlen(name) + 1;
    assert(table_size + size <= sizeof(table) && size <= UINT16_MAX);
    ACPI_IORT_NODE *n = (ACPI_IORT_NODE *)(table + table_size);
    n->Type = (UINT8)type;
    n->Length = (UINT16)size;
    if (name != NULL) {
        ACPI_IORT_NAMED_COMPONENT *nc = (ACPI_IORT_NAMED_COMPONENT *)n->NodeData;
        nc->MemoryAddressLimit = (UINT8)limit;
        memcpy(nc->DeviceName, name, strlen(name) + 1);
    }
    table_size += size;
    ACPI_TABLE_IORT *iort = (ACPI_TABLE_IORT *)table;
    iort->NodeCount++;
    iort->Header.Length = (UINT32)table_size;
}
static void fixture(bool match, unsigned int limit)
{
    assert(live == 0);
    allocations = cleanups = parses = table_reads = errors = 0;
    table_status = parse_status = AE_OK;
    module_dma = ancestor_dma = ancestor_cca_present = false;
    vpu_cca_present = true;
    vpu_cca = ancestor_cca = 0;
    parse_count = 0;
    memset(table, 0, sizeof(table));
    table_size = sizeof(ACPI_TABLE_IORT);
    ((ACPI_TABLE_IORT *)table)->NodeOffset = (UINT32)table_size;
    add_node(ACPI_IORT_NODE_ITS_GROUP, NULL, 0);
    add_node(ACPI_IORT_NODE_NAMED_COMPONENT, "\\_SB_.OTHR", 32);
    if (match) add_node(ACPI_IORT_NODE_NAMED_COMPONENT, "\\_SB_.VPU0", limit);
    memset(&vpu_info, 0, sizeof(vpu_info));
    memset(&module_info, 0, sizeof(module_info));
    memset(&ancestor_info, 0, sizeof(ancestor_info));
    vpu_info.Type = module_info.Type = ancestor_info.Type = ACPI_TYPE_DEVICE;
    module_info.HardwareId.String = "ACPI0004";
    ancestor_info.HardwareId.String = "ACPI0004";
    vpu = (struct acpi_devnode){NULL, &vpu_info, VPU_HANDLE, NULL};
    module = (struct acpi_devnode){NULL, &module_info, MODULE_HANDLE, NULL};
    ancestor = (struct acpi_devnode){NULL, &ancestor_info, ANCESTOR_HANDLE, NULL};
}
static void destroy_tag(bus_dma_tag_t tag)
{
    kmem_free(tag->_ranges, (size_t)tag->_nranges * sizeof(*tag->_ranges));
    kmem_free(tag, sizeof(*tag));
    CHECK(live == 0);
}
static void check_default(uintptr_t length, uint32_t flags)
{
    bus_dma_tag_t tag = arm_acpi_dma64_tag(&softc, &vpu);
    assert(tag != NULL);
    CHECK(tag != &arm_generic_dma_tag);
    CHECK(tag->_nranges == 1);
    CHECK(tag->_ranges[0].dr_sysbase == 0 && tag->_ranges[0].dr_busbase == 0);
    CHECK(tag->_ranges[0].dr_len == length);
    CHECK(tag->_ranges[0].dr_flags == flags);
    CHECK(allocations == 2 && live == 2);
    destroy_tag(tag);
}
static void test_iort(void)
{
    /* Wrong caller object must lose the real IORT limit and fail this range. */
    fixture(true, 40);
    check_default(UINT64_C(0xffffffffff), 0);
    CHECK(table_reads == 1 && parses == 0 && cleanups == 0);
    fixture(true, 32);
    vpu_cca = 1;
    check_default(UINT64_C(0xffffffff), _BUS_DMAMAP_COHERENT);
    fixture(false, 40);
    check_default(UINTPTR_MAX, 0);
    fixture(true, 0);
    check_default(UINTPTR_MAX, 0);
    fixture(true, 40);
    table_status = AE_NOT_FOUND;
    check_default(UINTPTR_MAX, 0);

    /* _CCA inheritance and the native absent-property default stay intact. */
    fixture(true, 40);
    vpu_cca_present = false;
    vpu.ad_parent = &ancestor;
    ancestor_cca_present = true;
    ancestor_cca = 1;
    check_default(UINT64_C(0xffffffffff), _BUS_DMAMAP_COHERENT);
    fixture(true, 40);
    vpu_cca_present = false;
    check_default(UINT64_C(0xffffffffff), _BUS_DMAMAP_COHERENT);

    /* The real lookup traverses earlier unmatched and non-named nodes. */
    fixture(true, 40);
    ACPI_IORT_NAMED_COMPONENT *out = NULL;
    CHECK(acpi_iort_named_component(VPU_HANDLE, &out) == AE_OK);
    CHECK(out != NULL && out->MemoryAddressLimit == 40);
    out = NULL;
    CHECK(acpi_iort_named_component(&vpu, &out) == AE_NOT_FOUND);
    CHECK(out == NULL);
    fixture(true, 40);
    bus_dma_tag_t cached = &arm_generic_dma_tag;
    vpu.ad_dmat64 = cached;
    CHECK(arm_acpi_dma64_tag(&softc, &vpu) == cached);
    CHECK(allocations == 0 && table_reads == 0 && parses == 0 && live == 0);
}
static void test_module(void)
{
    for (unsigned int coherent = 0; coherent < 2; coherent++) {
        fixture(true, 40);
        vpu.ad_parent = &module;
        module_dma = true;
        vpu_cca = coherent;
        parse_count = 2;
        parse_mem[0] = (struct acpi_mem){0x100000, 0x80000000, 0x20000};
        parse_mem[1] = (struct acpi_mem){0x400000, 0x90000000, 0x30000};
        bus_dma_tag_t tag = arm_acpi_dma64_tag(&softc, &vpu);
        CHECK(tag->_nranges == 2);
        CHECK(tag->_ranges[0].dr_sysbase == 0x80000000);
        CHECK(tag->_ranges[0].dr_busbase == 0x100000);
        CHECK(tag->_ranges[0].dr_len == 0x20000);
        CHECK(tag->_ranges[1].dr_sysbase == 0x90000000);
        CHECK(tag->_ranges[1].dr_busbase == 0x400000);
        CHECK(tag->_ranges[1].dr_len == 0x30000);
        CHECK(tag->_ranges[0].dr_flags == (coherent ? _BUS_DMAMAP_COHERENT : 0));
        CHECK(tag->_ranges[1].dr_flags == (coherent ? _BUS_DMAMAP_COHERENT : 0));
        CHECK(table_reads == 0 && parses == 1 && cleanups == 1 && errors == 0);
        CHECK(live == 2 && allocations == 3);
        destroy_tag(tag);
    }
    fixture(true, 40);
    vpu.ad_parent = &module;
    module_dma = true;
    check_default(UINT64_C(0xffffffffff), 0);
    CHECK(parses == 1 && cleanups == 1 && table_reads == 1);
    fixture(true, 40);
    vpu.ad_parent = &module;
    module_dma = true;
    parse_status = AE_ERROR;
    check_default(UINT64_C(0xffffffffff), 0);
    CHECK(parses == 1 && errors == 1 && table_reads == 1);
    fixture(true, 40);
    vpu.ad_parent = &module;
    module.ad_parent = &ancestor;
    ancestor_dma = true;
    check_default(UINT64_C(0xffffffffff), 0);
    CHECK(parses == 1 && cleanups == 1);
    fixture(true, 40);
    vpu.ad_parent = &module;
    module_info.Type = ACPI_TYPE_PROCESSOR;
    module_dma = true;
    check_default(UINT64_C(0xffffffffff), 0);
    CHECK(parses == 0);
}
int main(void)
{
    test_iort();
    test_module();
    CHECK(live == 0);
    printf("ACPI IORT DMA: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
