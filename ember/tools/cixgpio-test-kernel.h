/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD ACPI GPIO host environment model, AI-assisted. */
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef struct { unsigned int held; } kmutex_t;
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL {NULL}
struct test_cfdata { int cf_flags; };
typedef struct test_cfdata *cfdata_t;
struct acpi_devnode { ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void * const test_attach_refs[] = {(const void *)(m),(const void *)(a)}
#define MUTEX_DEFAULT 0
#define IPL_VM 5
#define __BIT(n) (UINT32_C(1)<<(n))
#define __arraycount(x) (sizeof(x)/sizeof((x)[0]))
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
static void test_print(const char *fmt, ...)
{ (void)fmt; }
#define aprint_normal_dev(d,...) ((void)(d),test_print(__VA_ARGS__))
static struct test_cfdata cf;
static unsigned int mapped, unmapped, irq_established, irq_disestablished;
static unsigned int reads[12], writes[12], write_count, last_write;
static uint32_t regs[12];
static bool map_fail, irq_fail, locked_queue, simulate_duplicate, bad_readback;
static void *private_data;
static void mutex_init(kmutex_t *m,int a,int b) { (void)a;(void)b;m->held=0; }
static void mutex_destroy(kmutex_t *m) { assert(!m->held); }
static void mutex_enter(kmutex_t *m) { assert(!m->held);m->held=1; }
static void mutex_exit(kmutex_t *m) { assert(m->held);m->held=0; }
static void *device_private(device_t d) { (void)d;return private_data; }
static cfdata_t device_cfdata(device_t d) { (void)d;return &cf; }
static const char *device_xname(device_t d) { (void)d;return "cixgpio0"; }
static int acpi_compatible_match(const void *a,const struct device_compatible_entry *e)
{ (void)a;CHECK(e[0].compat && strcmp(e[0].compat,"CIXH1003")==0); return 1; }
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE,const char *,ACPI_INTEGER *);
static ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE,const char *,ACPI_INTEGER *);
static int bus_space_map(bus_space_tag_t t,bus_addr_t a,bus_size_t n,int f,
    bus_space_handle_t *h)
{ (void)t;(void)f; CHECK(a==0x16004000 && n==0x1000); mapped++;*h=1;return map_fail; }
static void bus_space_unmap(bus_space_tag_t t,bus_space_handle_t h,bus_size_t n)
{ (void)t;(void)h;(void)n;unmapped++; }
static uint32_t bus_space_read_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t o)
{
    uint32_t v;
    (void)t;(void)h; if(o%4 || o>=48){CHECK(false);return 0;}
    reads[o/4]++;v=regs[o/4];
    if(o==0x2c && bad_readback && reads[o/4]>1)v|=0x40;
    if(o==0x20) regs[o/4]=0; /* Cadence status is clear-on-read. */
    return v;
}
static void bus_space_write_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t o,uint32_t v)
{
    (void)t;(void)h;if(o%4 || o>=48){CHECK(false);return;}
    writes[o/4]++;write_count++;last_write=(unsigned int)o;
    if(o==0x18) regs[0x14/4]&=~v;
    else if(o==0x1c) regs[0x14/4]|=v;
    else { CHECK(o!=0x20);regs[o/4]=v; }
}
static void *acpi_intr_establish_irq(device_t d,struct acpi_irq *i,int p,bool s,
    int (*f)(void *),void *a,const char *n)
{ (void)d;(void)p;(void)s;(void)f;(void)a;(void)n;CHECK(i->ar_irq==404);irq_established++;return irq_fail?NULL:TEST_HANDLE; }

