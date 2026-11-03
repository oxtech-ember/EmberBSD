/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual A133 GPU FDT preflight contract, AI-assisted. */
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun50i_a133_gpu_owner.h"
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define be32toh(v) __builtin_bswap32(v)
#define A133_GPU_BASE UINT64_C(0x01800000)
#define A133_GPU_SIZE UINT64_C(0x80000)
#define A133_GPU_DFS_REG UINT64_C(0x01880020)
#define A133_GPU_DFS_SIZE 4
#define A133_GPU_FW_SIZE 118784
#define A133_GPU_BVNC_REG 0x20
#define PVR_FW_PROFILE_GE8300 1
#define PVR_FW_FEATURE_NUM_CLUSTERS 28
#define PVR_FW_FEATURE_SLC_SIZE_IN_KILOBYTES 46
#define PVR_GE8300_BVNC UINT64_C(0x0016006600360026)
typedef unsigned int u_int;
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef uintptr_t bus_space_handle_t;
typedef void *firmware_handle_t;
struct pvr_fwinfo { int unused; };
struct pvr_dma { uint64_t address; size_t size; };
struct pvr_fwmem {
    struct { struct pvr_fwinfo info; } image;
    struct pvr_dma code, data, table, work, config;
};
struct pvr_fwboot_result { int unused; };
struct clk { int id; };
struct fdtbus_reset { int unused; };
struct fdtbus_regulator { int unused; };
struct a133_gpu_softc {
    int phandle; void *dmat, *bst;
    bus_space_handle_t bsh, dfs_bsh;
    bus_addr_t address, dfs_address;
    bus_size_t size, dfs_size;
    struct clk *core, *bus, *pll;
    struct fdtbus_reset *reset;
    struct fdtbus_regulator *supply;
    void *firmware; size_t firmware_size;
    bool mapped, dfs_mapped;
    uint32_t core_hz;
    uint64_t effective_hz;
    struct pvr_fwmem *memory;
    struct pvr_fwboot_result boot_result;
};
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
static struct clk core, bus, pll;
static struct fdtbus_reset reset;
static struct fdtbus_regulator regulator;
static const char *bad_prop;
static int bad_node, bad_stage, resources, mapped, opened, frees, hardware;
static int sequence[5], fail_step;
static int reads, allocations, boot_calls, reserve_form;
static uint64_t bvnc_value;
static uint32_t dfs_value;
static struct pvr_fwmem memory;
static int OF_parent(int n) { return n > 1 ? n - 1 : -1; }
static int OF_getproplen(int n, const char *key) {
    if (bad_prop != NULL && n == bad_node && strcmp(key, bad_prop) == 0) return 0;
    if (n == 3 && (!strcmp(key, "ember,a133-gpu-exclusive") ||
        !strcmp(key, "netbsd,consumer-managed-power"))) return bad_stage == 1 ? -1 : 0;
    if (n == 3 && !strcmp(key, "operating-points")) return 32;
    if (n == 3 && !strcmp(key, "interrupts")) return 12;
    if (n == 5 && !strcmp(key, "reg")) return reserve_form == 1 ? 32 :
        reserve_form == 3 ? 15 : reserve_form == 5 ? -1 : 16;
    return -1;
}
static int OF_getprop(int n, const char *key, void *dst, size_t len) {
    static const uint32_t opp[] = {504000,950000,472500,950000,441000,950000,252000,950000};
    static const uint32_t irq[] = {0,97,4};
    const uint32_t *src = !strcmp(key,"interrupts") ? irq : opp;
    (void)n;
    for (size_t i=0; i<len/4; i++) ((uint32_t *)dst)[i] = __builtin_bswap32(src[i]);
    if (bad_stage == 2) ((uint32_t *)dst)[len/4-1] ^= 1;
    return (int)len;
}
static int fdtbus_get_reg_byname(int n, const char *name, bus_addr_t *a, bus_size_t *z) {
    (void)n; *a = !strcmp(name,"gpu") ? A133_GPU_BASE : A133_GPU_DFS_REG;
    *z = !strcmp(name,"gpu") ? A133_GPU_SIZE : A133_GPU_DFS_SIZE;
    if (bad_stage == 3) (*a)++;
    return 0;
}
static struct clk *fdtbus_clock_get(int n, const char *name) {
    (void)n; resources++; return !strcmp(name,"core") ? &core : &bus;
}
static struct clk *clk_get_parent(struct clk *c) { (void)c; resources++; return &pll; }
static void clk_put(struct clk *c) { (void)c; resources--; }
static struct fdtbus_reset *fdtbus_reset_get(int n, const char *s) { (void)n;(void)s;resources++;return &reset; }
static void fdtbus_reset_put(struct fdtbus_reset *r) { (void)r;resources--; }
static struct fdtbus_regulator *fdtbus_regulator_acquire(int n, const char *s) { (void)n;(void)s;resources++;return &regulator; }
static void fdtbus_regulator_release(struct fdtbus_regulator *r) { (void)r;resources--; }
static int fdtbus_regulator_is_enabled(struct fdtbus_regulator *r, bool *b) { (void)r;*b = bad_stage != 4;return 0; }
static int fdtbus_regulator_get_voltage(struct fdtbus_regulator *r, u_int *u) { (void)r;*u = bad_stage == 5 ? 900000 : 950000;return 0; }
static int bus_space_map(void *t, bus_addr_t a, bus_size_t z, int flags, bus_space_handle_t *h) {
    (void)t;(void)a;(void)z;(void)flags;
    if (bad_stage == 6 && mapped == 1) return EIO;
    *h = ++mapped; return 0;
}
static void bus_space_unmap(void *t, bus_space_handle_t h, bus_size_t z) { (void)t;(void)h;(void)z;mapped--; }
static int firmware_open(const char *a,const char *b,firmware_handle_t *f) {
    (void)a;(void)b;if (bad_stage == 7) return ENOENT; *f=&opened;opened++;return 0;
}
static long firmware_get_size(firmware_handle_t f) { (void)f;return bad_stage == 8 ? 3 : A133_GPU_FW_SIZE; }
static void *firmware_malloc(size_t n) { return malloc(n); }
static void firmware_free(void *p,size_t n) { (void)n;free(p);frees++; }
static int firmware_read(firmware_handle_t f, int o, void *p,size_t n) {
    (void)f;(void)o;(void)p;(void)n;return bad_stage == 9 ? EIO : 0;
}
static int firmware_close(firmware_handle_t f) { (void)f;opened--;return bad_stage == 10 ? EIO : 0; }
static int activation_step(int value) {
    sequence[hardware++] = value;
    return hardware == fail_step ? EIO : 0;
}
static int clk_enable(struct clk *c) {
    return activation_step(c == &pll ? 1 : c == &bus ? 3 : 4);
}
static unsigned clk_get_rate(struct clk *c) { (void)c; return 504000000; }
static int fdtbus_reset_deassert(struct fdtbus_reset *r) { (void)r;return activation_step(2); }
static int fdtbus_powerdomain_enable(int n) { (void)n;return activation_step(5); }
static int fdtbus_powerdomain_is_enabled_index(int n,int i,bool *b) { (void)n;(void)i;*b=true;return 0; }
static int bus_space_peek_8(void *t, bus_space_handle_t h, unsigned off, uint64_t *v) {
    (void)t; CHECK(h == 1 && off == A133_GPU_BVNC_REG && hardware == 5);
    CHECK(reads++ % 2 == 0); *v = bvnc_value; return 0;
}
static int bus_space_peek_4(void *t, bus_space_handle_t h, unsigned off, uint32_t *v) {
    (void)t; CHECK(h == 2 && off == 0 && hardware == 5);
    CHECK(reads++ % 2 == 1); *v = dfs_value; return 0;
}
static const void *fdtbus_get_data(void) { return &memory; }
static int fdt_check_header(const void *f) { return f == NULL ? -1 : 0; }
static int fdt_num_mem_rsv(const void *f) { (void)f;return 2; }
static int fdt_get_mem_rsv(const void *f,int i,uint64_t *a,uint64_t *z) {
    (void)f;
    if ((i == 0) != (reserve_form == 2)) { *a=0x48000000; *z=0x1000000; }
    else { *a=0x48100000; *z=0x100000; }
    return 0;
}
static int OF_finddevice(const char *s) { (void)s;return 4; }
static int OF_child(int n) { return n == 4 ? 5 : 0; }
static int OF_peer(int n) { (void)n;return 0; }
static bool fdtbus_status_okay(int n) { (void)n;return true; }
static int of_getprop_uint32(int n,const char *s,uint32_t *v) { (void)n;(void)s;*v=reserve_form == 4 ? 0 : 2;return 0; }
static int fdtbus_get_reg64(int n,unsigned i,uint64_t *a,uint64_t *z) {
    (void)n;*a=0x61000000 + i*4096;
    *z=4096;return 0;
}
static int pvr_fwmem_prepare(void *d,const void *b,size_t n,uint64_t v,int profile,
    uint64_t pa,uint64_t size,uint64_t aux,uint64_t auxsize,struct pvr_fwmem **out) {
    (void)d;(void)b; CHECK(n == A133_GPU_FW_SIZE && v == PVR_GE8300_BVNC && profile == 1);
    CHECK(pa == A133_GPU_BASE && size == A133_GPU_SIZE && aux == A133_GPU_DFS_REG && auxsize == 4);
    memory.code=(struct pvr_dma){0x40000000,4096};
    memory.data=(struct pvr_dma){0x40001000,4096};
    memory.table=(struct pvr_dma){0x40002000,4096};
    memory.work=(struct pvr_dma){0x40003000,4096};
    memory.config=(struct pvr_dma){0x40004000,4096};
    allocations++; *out = &memory; return 0;
}
static int pvr_fwinfo_feature(const struct pvr_fwinfo *i,int feature,uint64_t *v) {
    (void)i; *v = feature == PVR_FW_FEATURE_NUM_CLUSTERS ? 1 : 64; return 0;
}
static int pvr_fwmem_interface(struct pvr_fwmem *m,uint64_t hz,uint32_t clusters,uint32_t slc) {
    CHECK(m == &memory && hz == 472500000 && clusters == 1 && slc == 64); return 0;
}
static int pvr_fwboot(int profile,void *t,bus_space_handle_t h,struct pvr_fwmem *m,
    struct pvr_fwboot_result *r) {
    (void)t;(void)r;(void)reserve_form;CHECK(profile == 1 && h == 1 && m == &memory);boot_calls++;return 0;
}
static int pvr_ge8300_fwinfo_parse(const void *p,size_t n,uint64_t b,struct pvr_fwinfo *i) {
    (void)p;(void)n;(void)b;(void)i;return bad_stage == 11 ? EOPNOTSUPP : 0;
}
#include "preflight.inc"
int main(void) {
    struct a133_gpu_softc sc;
    const char *const keys[] = {"iommus","iommu-map","dma-ranges"};
    for (int n=1;n<=3;n++) for (size_t i=0;i<__arraycount(keys);i++) {
        memset(&sc,0,sizeof(sc)); sc.phandle=3; sc.dmat=&sc;
        bad_node=n; bad_prop=keys[i];
        CHECK(a133_gpu_preflight(&sc) == EOPNOTSUPP);
        CHECK(resources==0 && mapped==0 && opened==0 && hardware==0);
    }
    bad_prop="memory-region"; bad_node=3;
    memset(&sc,0,sizeof(sc));sc.phandle=3;sc.dmat=&sc;
    CHECK(a133_gpu_preflight(&sc) == EOPNOTSUPP);
    bad_prop=NULL;
    for (bad_stage=1;bad_stage<=11;bad_stage++) {
        memset(&sc,0,sizeof(sc));sc.phandle=3;sc.dmat=&sc;
        CHECK(a133_gpu_preflight(&sc) != 0);
        CHECK(resources==0 && mapped==0 && opened==0 && hardware==0);
    }
    bad_stage=0;
    for (reserve_form=1;reserve_form<=5;reserve_form++) {
        if (reserve_form == 2) continue;
        memset(&sc,0,sizeof(sc));sc.phandle=3;sc.dmat=&sc;
        CHECK(a133_gpu_preflight(&sc) == EOPNOTSUPP);
        CHECK(resources==0 && mapped==0 && opened==0 && hardware==0);
    }
    reserve_form=2;memset(&sc,0,sizeof(sc));sc.phandle=3;sc.dmat=&sc;
    CHECK(a133_gpu_preflight(&sc) == 0);
    a133_gpu_preflight_release(&sc);
    CHECK(resources==0 && mapped==0 && opened==0);
    reserve_form=0;memset(&sc,0,sizeof(sc));sc.phandle=3;sc.dmat=&sc;
    CHECK(a133_gpu_preflight(&sc) == 0);
    CHECK(resources==5 && mapped==2 && opened==0 && sc.firmware != NULL);
    for (int failure=1;failure<=5;failure++) {
        hardware=0;fail_step=failure;memset(sequence,0,sizeof(sequence));
        CHECK(a133_gpu_activate(&sc) == EIO);
        CHECK(hardware == failure);
        for (int i=0;i<failure;i++) CHECK(sequence[i] == i+1);
        CHECK(resources==5 && mapped==2 && sc.firmware != NULL);
    }
    hardware=0;fail_step=0;
    CHECK(a133_gpu_activate(&sc) == 0 && hardware == 5);
    for (int i=0;i<5;i++) CHECK(sequence[i] == i+1);
    bvnc_value=PVR_GE8300_BVNC+1; dfs_value=2; reads=0; allocations=0; boot_calls=0;
    CHECK(a133_gpu_prepare(&sc) == ENODEV && reads == 1 && allocations == 0);
    bvnc_value=PVR_GE8300_BVNC; dfs_value=3; reads=0;
    CHECK(a133_gpu_prepare(&sc) == EOPNOTSUPP && reads == 2 && allocations == 0);
    dfs_value=2; reads=0;
    CHECK(a133_gpu_prepare(&sc) == 0 && reads == 2 && allocations == 1);
    CHECK(sc.firmware == NULL && sc.firmware_size == 0);
    bad_stage=5;
    CHECK(a133_gpu_start(&sc) == EBUSY && boot_calls == 0);
    bad_stage=0;
    CHECK(a133_gpu_start(&sc) == 0 && boot_calls == 1);
    hardware=0;
    a133_gpu_preflight_release(&sc);
    CHECK(resources==0 && mapped==0 && opened==0 && hardware==0 && frees > 0);
    puts("A133 actual FDT preflight and early cleanup: PASS");
    return 0;
}
