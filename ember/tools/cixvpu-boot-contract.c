/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual control/boot source; platform services only are modeled. */
#define cixvpu_acpi_boot model_cixvpu_acpi_boot
#define cixvpu_acpi_boot_create model_cixvpu_acpi_boot_create
#define cixvpu_acpi_boot_once model_cixvpu_acpi_boot_once
#define cixvpu_acpi_boot_report model_cixvpu_acpi_boot_report
#define main cixvpu_control_contract_main
#define cixvpu_dma model_dma
#define cixvpu_dma_page model_dma_page
#define cixvpu_dma_alloc model_dma_alloc
#define cixvpu_dma_free model_dma_free
#define cixvpu_prepared model_prepared
#define cixvpu_prepare_cpu model_prepare_cpu
#define cixvpu_prepare_boot_cpu model_prepare_boot_cpu
#define cixvpu_prepared_free model_prepared_free
#define cixvpu_prepared_sync_initial model_prepared_sync_initial
#define cixvpu_prepared_publish_root model_prepared_publish_root
#define cixvpu_prepared_boot_receive model_prepared_boot_receive
#define cixvpu_boot_reply model_boot_reply
#define delay control_delay
#define bus_space_read_4 control_read_4
#define bus_space_barrier control_barrier
#include "cixvpu-control-contract.c"
#undef main
#undef cixvpu_acpi_boot
#undef cixvpu_acpi_boot_create
#undef cixvpu_acpi_boot_once
#undef cixvpu_acpi_boot_report
#undef cixvpu_dma
#undef cixvpu_dma_page
#undef cixvpu_dma_alloc
#undef cixvpu_dma_free
#undef cixvpu_prepared
#undef cixvpu_prepare_cpu
#undef cixvpu_prepare_boot_cpu
#undef cixvpu_prepared_free
#undef cixvpu_prepared_sync_initial
#undef cixvpu_prepared_publish_root
#undef cixvpu_prepared_boot_receive
#undef cixvpu_boot_reply
#undef bus_space_read_4
#undef bus_space_barrier
#undef delay
#ifdef CIXVPU_BOOT_ORCH_API
#include <sys/time.h>
#undef _DEV_IC_CIXVPU_PREPARE_H_
struct cixvpu_dma;
#include "real-prepare-header.inc"
#include "boot-header.inc"
extern struct cixvpu_prepared *boot_fixture_prepare(void);
extern void boot_fixture_runtime(bool);
extern bool boot_fixture_exposed(struct cixvpu_prepared *);
extern void boot_fixture_drop(struct cixvpu_prepared *);
extern struct cixvpu_prepared *boot_fixture_prepare_kind(bool);
extern void boot_fixture_set_ack(void (*)(void));
extern unsigned int boot_fixture_acks(void);
extern unsigned int boot_fixture_live(void);
extern void boot_fixture_map_valid(struct cixvpu_prepared *, bool);

/* The PIC model starts masked and can dispatch synchronously on unmask. */
#define KM_NOSLEEP 0
#define IPL_VM 6
#define IST_LEVEL 3
#define IST_MPSAFE 0x100
#define BUS_SPACE_BARRIER_WRITE 2
static unsigned int boot_masks, boot_unmasks, boot_intrs, boot_disestablish;
static unsigned int boot_irq_count, boot_writes, boot_delays;
static unsigned int boot_fault;
static bool boot_term_issued;
static uint64_t boot_now;
static uint32_t boot_regs[2][0x400 / 4];
static struct { bus_space_handle_t h; bus_size_t offset; uint32_t value; } boot_trace[64];
static unsigned int boot_barriers;
static int (*boot_handler)(void *);
static void *boot_handler_arg;
static unsigned int boot_pic_mask_count;
static struct cixvpu_prepared *boot_actual;
static void *kmem_zalloc(size_t n, int f)
{ (void)f; if (boot_fault == 1) return NULL; return allocate(n); }
static unsigned int atomic_cas_uint(volatile unsigned int *p, unsigned int a, unsigned int b)
{ unsigned int old = *p; if (old == a) *p = b; return old; }
static void atomic_or_uint(volatile unsigned int *p, unsigned int v) { *p |= v; }
#define atomic_load_acquire(p) (*(p))
#define atomic_store_release(p,v) (*(p)=(v))
static void *intr_establish_xname_masked(int irq, int ipl, int type,
    int (*fn)(void *), void *arg, const char *name)
{
    CHECK(irq == 358 && ipl == IPL_VM && type == (IST_LEVEL | IST_MPSAFE));
    CHECK(name != NULL && fn != NULL && arg != NULL); boot_intrs++;
    CHECK(maps==0 && !power_live && !clock_live);
    assert(fn != NULL && arg != NULL);
    if (boot_fault==22) CHECK(fn(arg)==0 && boot_writes==0);
    if (boot_fault == 2) return NULL;
    CHECK(boot_handler == NULL); boot_handler = fn; boot_handler_arg = arg;
    boot_pic_mask_count = 1; return &boot_handler;
}
static void intr_mask(void *cookie)
{ CHECK(cookie == &boot_handler && boot_pic_mask_count == 0); boot_pic_mask_count++; boot_masks++; }
static void intr_unmask(void *cookie)
{
    CHECK(cookie == &boot_handler && boot_pic_mask_count == 1);
    boot_pic_mask_count--; boot_unmasks++;
    if (boot_fault == 6) faults |= F_RESET_ASSERT;
    if (boot_fault>=13 && boot_fault<=20) {
        static const unsigned int cleanup[]={F_RESET_ASSERT,F_CLOCK_OFF,F_POWER_OFF,
            F_STA_OFF,F_STA_OFF_FAIL,F_PUT_MAIN,F_PUT_RCSU,F_PUT_CLOCK};
        faults |= cleanup[boot_fault-13];
    }
    if (boot_fault == 3) { boot_regs[1][0x18 / 4] = 2; boot_handler(boot_handler_arg); }
}
static void intr_disestablish(void *cookie)
{ CHECK(cookie == &boot_handler && boot_pic_mask_count == 1); boot_disestablish++; boot_handler = NULL; }
static uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset)
{
    (void)t; CHECK((h == 1 || h == 2) && offset < 0x400 && offset % 4 == 0);
    if (h == 1 && offset == 0x218) {
        boot_irq_count++;
        if (!boot_term_issued || boot_fault==4 || boot_fault==11 || boot_fault==12)
            return boot_regs[1][offset / 4];
        return 0;
    }
    return boot_regs[h == 1 ? 1 : 0][offset / 4];
}
static void boot_final_ack_irq(void);
static void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
    (void)t; CHECK((h == 1 || h == 2) && offset < 0x400 && offset % 4 == 0);
    CHECK(boot_writes==boot_barriers);
    assert(boot_writes < sizeof(boot_trace)/sizeof(boot_trace[0]));
    boot_trace[boot_writes].h=h; boot_trace[boot_writes].offset=offset;
    boot_trace[boot_writes++].value=value;
    if (h==1 && offset==0x204 && boot_fault==10) boot_regs[1][0x14/4]=0;
    if (h == 1 && offset == 0x204) CHECK(boot_fixture_exposed(boot_actual));
    boot_regs[h == 1 ? 1 : 0][offset / 4] = value;
    if (h==1 && offset==0x218 && value==1) boot_term_issued=true;
    if (h==1 && offset==4 && value==1) {
        boot_fixture_runtime(boot_fault != 7 && boot_fault != 5 && boot_fault != 21);
        if (boot_fault == 9) boot_fixture_set_ack(boot_final_ack_irq);
    }
    if (h == 1 && offset == 0x21c && value == 0) boot_regs[1][0x18 / 4] &= ~1U;
}
static void bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, bus_size_t length, int flags)
{ (void)t; CHECK(h == 1 || h == 2); CHECK(offset <= 0x10000 && length <= 0x10000-offset); CHECK(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)); CHECK(boot_writes==boot_barriers+1); CHECK(boot_trace[boot_barriers].h==h && boot_trace[boot_barriers].offset==offset && length==4); boot_barriers++; }
static void getmicrouptime(struct timeval *t)
{ if (boot_fault==11 || boot_fault==21) boot_now=boot_now==0 ? 100000 : boot_now-100; else if (boot_fault!=5 && boot_fault!=12) boot_now+=10; t->tv_sec=boot_now/1000000; t->tv_usec=boot_now%1000000; }
static void delay(unsigned int usec)
{ boot_delays++; if (boot_fault!=5 && boot_fault!=12 && boot_fault!=11) boot_now+=usec; }
static const char *device_xname(device_t d) { CHECK(d==VPU); return "cixvpu0"; }
static void boot_final_ack_irq(void)
{
    if (boot_fixture_acks() == 2) {
        boot_regs[1][0x18 / 4] = 3;
        CHECK(boot_handler(boot_handler_arg) == 1);
    }
}
static int boot_publish_actual(struct cixvpu_prepared *p, uint32_t *root)
{
    int error = cixvpu_prepared_publish_root(p, root);
    if (error == 0 && boot_fault == 8) {
        boot_regs[1][0x18 / 4] = 2;
        CHECK(boot_handler(boot_handler_arg) == 1);
    }
    return error;
}
#define cixvpu_prepared_publish_root boot_publish_actual
#include "boot-source.inc"
#undef cixvpu_prepared_publish_root

static void boot_fixture(void)
{
    profile_fixture(); cold = 0;
    boot_core_bad=0; boot_core_on_fail=boot_core_off_fail=boot_core_no_lease=-1; memset(boot_core_live,0,sizeof(boot_core_live));
    boot_masks = boot_unmasks = boot_intrs = boot_disestablish = 0;
    boot_irq_count = boot_writes = boot_delays = boot_barriers = 0; boot_now = 0; boot_fault = 0;
    boot_handler = NULL; boot_handler_arg = NULL; boot_pic_mask_count = 0;
    boot_term_issued=false;
    memset(boot_regs, 0, sizeof(boot_regs));
    boot_regs[0][0x21c/4] = 0xa5a50ffc;
    boot_regs[0][0x300/4] = 0xf1234567;
    boot_regs[1][0x14/4] = 0x0f0f0f0f;
    boot_regs[1][0x10/4] = UINT32_MAX;
}
static struct cixvpu_acpi_boot *boot_new(bool special)
{
    struct cixvpu_acpi_boot *ctx = NULL;
    CHECK(cixvpu_acpi_boot_create(VPU, &fixture_aa, &attach_description, &ctx) == 0);
    assert(ctx != NULL);
    boot_actual = boot_fixture_prepare_kind(special);
    assert(boot_actual != NULL);
    return ctx;
}
static void boot_discard_fixture(struct cixvpu_acpi_boot *ctx)
{
    /* End assertions before host-only quarantine teardown; no driver bypass. */
    if (boot_fixture_live()!=0) boot_fixture_drop(boot_actual);
    CHECK(boot_fixture_live()==0);
    AcpiOsFree(ctx);
}
static void boot_failure_tests(void)
{
    struct cixvpu_acpi_boot *ctx;
    struct cixvpu_prepared *owned;
    struct cixvpu_boot_report r = {0};
    unsigned int before;

    /* Real ordinary constructor: root refuses at its actual typed boundary. */
    boot_fixture(); ctx=boot_new(false); owned=boot_actual; boot_fault=6;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(!r.exposed && r.cleanup_error==EIO && r.prepared_retained);
    CHECK(boot_pic_mask_count==1 && boot_masks==1 && ctx->irq_ready==0);
    CHECK(ctx->control.mapped && ctx->control.profile_mapped && power_live);
    CHECK(boot_disestablish==0 && boot_unmasks==1);
    boot_discard_fixture(ctx);

    /* Actual final HOST cursor PREWRITE publishes an asynchronous IRQ fault. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=9;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.exposed && r.switched_in && r.pong && r.irq_unexpected==2);
    CHECK(r.error==EIO && r.irq_seen==1 && r.irq_masked);
    CHECK(boot_regs[1][0x18/4]==2);
    CHECK(boot_handler(boot_handler_arg)==0 && boot_masks==1);
    CHECK(boot_masks==1 && boot_disestablish==0 && ctx->control.mapped);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    /* Fault after actual capability success but before its caller resumes. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=8;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.exposed && r.prepared_retained && r.irq_registered && r.irq_masked);
    CHECK(boot_writes>0 && boot_regs[1][0x204/4]==0 && boot_disestablish==0);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    /* Repeated sync is rejected by the real allocator before hardware/MOVE. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual;
    CHECK(cixvpu_prepared_sync_initial(owned)==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EBUSY && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.started && !r.moved && !r.exposed && !r.irq_registered);
    CHECK(nevents==0 && maps==0 && boot_writes==0 && boot_intrs==0);
    before=boot_writes; CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EALREADY);
    CHECK(owned==boot_actual && boot_writes==before);
    boot_discard_fixture(ctx);
}
static void boot_matrix_tests(void)
{
    struct cixvpu_acpi_boot *ctx;
    struct cixvpu_prepared *owned;
    struct cixvpu_boot_report r = {0};
    int error;
    const unsigned int setup_faults[]={F_CLOCK_GET,F_RESET_GET,F_RCSU_GET,
        F_RCSU_STATE,F_POWER_ON,F_STA_ON,F_STA_ON_FAIL,F_CLOCK_ON,
        F_RESET_ASSERT,F_RESET_DEASSERT,F_MAP,F_PROFILE_MAP,F_NATIVE_POISON,
        F_POWER_NO_LEASE};
    for(unsigned int i=0;i<sizeof(setup_faults)/sizeof(setup_faults[0]);i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; faults=setup_faults[i];
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.started && r.moved && !r.exposed);
        CHECK(boot_writes==0 && boot_unmasks==0);
        CHECK(!r.irq_registered || r.irq_masked);
        CHECK(r.prepared_retained == (r.cleanup_error!=0));
        if(r.cleanup_error!=0) CHECK(r.held && boot_fixture_live()!=0);
        else CHECK(!r.held && boot_fixture_live()==0 && r.control.released);
        boot_discard_fixture(ctx);
    }
    const unsigned int metadata_bad[]={1,7,10,16,25,26,29,39,42};
    for(unsigned int i=0;i<sizeof(metadata_bad)/sizeof(metadata_bad[0]);i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; bad=metadata_bad[i];
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(nevents==0 && boot_intrs==0 && maps==0 && boot_writes==0);
        CHECK(!r.prepared_retained && boot_fixture_live()==0);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=1;i<=3;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_bad=i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(boot_intrs==0 && maps==0 && boot_writes==0 && !r.prepared_retained);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<4;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_on_fail=(int)i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(r.core_attempted==((1U<<(i+1))-1) && r.core_released==r.core_attempted);
        CHECK(boot_writes==1 && boot_unmasks==0 && boot_disestablish==1);
        for(unsigned int j=0;j<4;j++) CHECK(!boot_core_live[j]);
        /* Exact reverse OFF sequence follows the checked reset/clock stop. */
        char expected[8]; unsigned int n=0;
        for(int j=(int)i;j>=0;j--) expected[n++]=(char)('4'+j);
        expected[n]=0; CHECK(strstr(events,expected)!=NULL);
        boot_discard_fixture(ctx);
    }
    const unsigned int cleanup_faults[]={F_RESET_ASSERT,F_CLOCK_OFF,F_POWER_OFF,
        F_STA_OFF,F_STA_OFF_FAIL,F_PUT_MAIN,F_PUT_RCSU,F_PUT_CLOCK};
    for(unsigned int i=0;i<sizeof(cleanup_faults)/sizeof(cleanup_faults[0]);i++) {
        boot_fixture(); ctx=boot_new(false); owned=boot_actual;
        /* Inject only after successful setup/unmask, at the real PIC boundary. */
        boot_fault=13+i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error!=0);
        CHECK(r.prepared_retained && r.held && r.irq_masked);
        CHECK(ctx->irq_ready==0 && boot_masks==1);
        CHECK((i==0 && boot_disestablish==0) || (i!=0 && boot_disestablish==1));
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<4;i++) {
        boot_fixture(); ctx=boot_new(false); owned=boot_actual; boot_core_off_fail=(int)i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.cleanup_error==EIO && !r.exposed);
        CHECK(r.core_attempted==15 && boot_core_live[i] && !r.control.released);
        CHECK((r.core_released & (1U<<i))==0 && r.prepared_retained);
        CHECK(ctx->control.mapped && ctx->control.profile_mapped && power_live);
        boot_discard_fixture(ctx);
    }
    for(unsigned int mode=2;mode<=12;mode++) {
        if(mode==6 || mode==8 || mode==9) continue;
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=mode;
        error=cixvpu_acpi_boot_once(ctx,&owned);
        CHECK(error!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
        if(mode==2 || mode==3 || mode==4 || mode==11 || mode==12) {
            CHECK(!r.exposed && !r.prepared_retained && r.cleanup_error==0);
            CHECK(boot_fixture_live()==0 && r.control.released);
        } else {
            CHECK(r.exposed && r.prepared_retained && r.held && r.irq_masked);
            CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY && boot_disestablish==0);
        }
        if(mode==5) CHECK(r.visits==1001 && error==ETIMEDOUT && boot_now==0);
        if(mode==7) CHECK(r.visits>0 && r.visits<=1001 && error==ETIMEDOUT);
        if(mode==4) CHECK(r.terminate_reads>0 && r.terminate_reads<=10001 && error==ETIMEDOUT);
        if(mode==12) CHECK(r.terminate_reads==10001 && error==ETIMEDOUT && boot_now==0);
        if(mode==11) CHECK(r.terminate_reads==1 && error==EIO);
        if(mode==10) CHECK(boot_regs[1][1]==0 && error==EBUSY);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<7;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual;
        switch(i) {
        case 0: boot_regs[1][1]=2; break;
        case 1: boot_regs[1][0x10/4]=0xfffffffe; break;
        case 2: boot_regs[1][0x18/4]=2; break;
        case 3: boot_regs[1][0x20c/4]=1; break;
        case 4: boot_regs[1][0x214/4]=1; break;
        case 5: boot_regs[1][0x14/4]=0x0f0f000f; break;
        case 6: boot_regs[1][0x218/4]=1; break;
        }
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EBUSY && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(boot_writes==0 && boot_unmasks==0 && boot_fixture_live()==0);
        boot_discard_fixture(ctx);
    }
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fixture_map_valid(owned,false);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.started && !r.moved && !r.exposed);
    CHECK(nevents==0 && boot_intrs==0 && maps==0 && boot_writes==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EALREADY);
    boot_fixture_map_valid(owned,true);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_no_lease=2;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==EIO);
    CHECK(r.core_attempted==7 && r.core_released==0 && r.prepared_retained && r.held);
    CHECK(boot_core_live[0] && boot_core_live[1] && !boot_core_live[2]);
    CHECK(r.core_status[2]==AE_NOT_FOUND && r.core_cleanup[2]==AE_ABORT_METHOD);
    CHECK(ctx->control.mapped && power_live && clock_claim.claimed);
    boot_discard_fixture(ctx);
}
static void boot_trace_api_tests(void)
{
    struct cixvpu_acpi_boot *ctx=NULL;
    struct cixvpu_prepared *owned=NULL;
    struct cixvpu_boot_report r={0}, before;
    unsigned int allocated;
    static const struct { unsigned int h, offset, value; } expected[]={
        {2,0x21c,0xa5a50ffd},{2,0x300,0x01234567},
        {1,0x50,1},{1,0x24,0},{1,0x44,2},{1,0x20c,1},
        {1,0x218,1},{1,0x200,0x1fe},{1,0x204,0},
        {1,0x210,0},{1,0x230,0x33},{1,0x234,0x22},
        {1,0x238,0x33},{1,0x23c,0x33},{1,0x21c,0},
        {1,0x220,1},{1,0x214,1},{1,4,0},{1,0x14,0xff8f7f00},{1,4,1}
    };

    boot_fixture(); allocated=allocations;
    CHECK(cixvpu_acpi_boot_create(NULL,&fixture_aa,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,NULL,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,NULL,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,NULL)==EINVAL);
    CHECK(ctx==NULL && allocations==allocated && maps==0 && nevents==0);
    interrupt_context=true;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    interrupt_context=false; soft_context=true;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    soft_context=false; boot_fault=1;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==ENOMEM && ctx==NULL);
    boot_fault=0; ctx=boot_new(true); owned=boot_actual;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EBUSY);
    CHECK(cixvpu_acpi_boot_once(NULL,&owned)==EINVAL && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_once(ctx,NULL)==EINVAL && !ctx->report.started);
    CHECK(cixvpu_acpi_boot_report(NULL,&r)==EINVAL);
    CHECK(cixvpu_acpi_boot_report(ctx,NULL)==EINVAL);
    CHECK(cixvpu_acpi_boot_report(ctx,&ctx->report)==EINVAL);
    memset(&r,0xa5,sizeof(r)); before=r;
    interrupt_context=true;
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==EWOULDBLOCK && memcmp(&r,&before,sizeof(r))==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EWOULDBLOCK && owned==boot_actual);
    interrupt_context=false; soft_context=true;
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==EWOULDBLOCK && memcmp(&r,&before,sizeof(r))==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EWOULDBLOCK && owned==boot_actual);
    soft_context=false;
    CHECK(!ctx->report.started && boot_writes==0 && boot_intrs==0 && nevents==0);
    boot_regs[1][0x14/4]=0xff8f7f6f; boot_fault=22;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==0 && owned==NULL);
    CHECK(boot_writes==sizeof(expected)/sizeof(expected[0]) && boot_barriers==boot_writes);
    for(unsigned int i=0;i<sizeof(expected)/sizeof(expected[0]);i++) {
        CHECK(boot_trace[i].h==expected[i].h && boot_trace[i].offset==expected[i].offset);
        if(i!=8) CHECK(boot_trace[i].value==expected[i].value);
        else CHECK(boot_trace[i].value!=0 && boot_fixture_exposed(boot_actual));
    }
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.moved && r.exposed && r.held);
    CHECK(r.stage==CIXVPU_BOOT_HELD && r.last_stage==CIXVPU_BOOT_RECEIVE && r.error==0);
    CHECK(r.core_attempted==15 && r.core_released==0 && r.prepared_retained);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=21;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.exposed && r.visits==1);
    CHECK(r.prepared_retained && r.irq_masked && boot_disestablish==0);
    boot_discard_fixture(ctx);

    for(unsigned int i=0;i<7;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual;
        switch(i) {
        case 0: ctx->resources.mem[0].base+=4; break;
        case 1: ctx->resources.mem[1].translated+=4; break;
        case 2: ctx->resources.mem[1].length+=4; break;
        case 3: ctx->resources.cca=1; break;
        case 4: ctx->resources.irq++; break;
        case 5: ctx->resources.irq_trigger=1; break;
        case 6: ctx->resources.nmem=1; break;
        }
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(nevents==0 && maps==0 && boot_intrs==0 && boot_writes==0);
        boot_discard_fixture(ctx);
    }
}
static void boot_tests(void)
{
    boot_fixture();
    struct cixvpu_acpi_boot *ctx = NULL;
    CHECK(cixvpu_acpi_boot_create(VPU, &fixture_aa, &attach_description, &ctx) == 0 && ctx != NULL);
    if (ctx == NULL) return;
    boot_actual = boot_fixture_prepare();
    struct cixvpu_prepared *owned = boot_actual;
    CHECK(cixvpu_acpi_boot_once(ctx, &owned) == 0 && owned == NULL);
    struct cixvpu_boot_report report = {0};
    CHECK(cixvpu_acpi_boot_report(ctx, &report) == 0);
    CHECK(report.exposed && report.switched_in && report.pong && report.irq_masked);
    CHECK(boot_intrs == 1 && boot_unmasks == 1 && boot_masks == 1 && boot_disestablish == 0);
    CHECK(boot_regs[0][0x21c/4] == 0xa5a50ffd && boot_regs[0][0x300/4] == 0x01234567);
    CHECK(boot_regs[1][0x200/4] == 0x1fe && boot_regs[1][0x44/4] == 2);
    CHECK(boot_regs[1][0x230/4] == 0x33 && boot_regs[1][0x234/4] == 0x22);
    CHECK(boot_regs[1][0x238/4] == 0x33 && boot_regs[1][0x23c/4] == 0x33);
    CHECK(boot_regs[1][0x14/4] == 0x0f0f0f00 && boot_regs[1][1] == 1);
    CHECK(power_live && clock_live && clock_claim.claimed && main_claim.claimed);
    unsigned int writes = boot_writes;
    CHECK(cixvpu_acpi_boot_once(ctx, &owned) == EALREADY && boot_writes == writes);
    CHECK(cixvpu_prepared_free(&boot_actual) == EBUSY);
    /* Only fixture teardown after all permanent-hold assertions. */
    boot_fixture_drop(boot_actual); AcpiOsFree(ctx);
}
#endif
int main(void)
{
    int result = cixvpu_control_contract_main();
#ifdef CIXVPU_BOOT_ORCH_API
    boot_tests(); boot_failure_tests(); boot_matrix_tests(); boot_trace_api_tests();
#else
    CHECK(!"dedicated native boot epoch is absent");
#endif
    printf("cixvpu boot: %u checks, %u failures\n", checks, failures);
    return result || failures ? 1 : 0;
}
