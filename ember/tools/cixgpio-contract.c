/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual ACPI GPIO event source contracts, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>
#include <accommon.h>

static char handle_token, argument_token;
#define TEST_HANDLE ((void *)&handle_token)
#define TEST_ARGUMENT ((void *)&argument_token)
typedef void *device_t;
#define KM_SLEEP 0
#define KASSERT assert
#define __KERNEL_RCSID(a,b)
static unsigned int checks, failures, masks, unmasks, event_evaluated, completed, live_events;
static ACPI_STATUS evaluate_result, queue_result;
static ACPI_OSD_EXEC_CALLBACK queued;
static void *queued_arg;
void *AcpiOsAllocate(ACPI_SIZE n) { return malloc(n); }
void AcpiOsFree(void *p) { free(p); }
void AcpiError(const char *m,UINT32 l,const char *f,...)
{ (void)m;(void)l;(void)f; }
void AcpiWarning(const char *m,UINT32 l,const char *f,...)
{ (void)m;(void)l;(void)f; }
void AcpiException(const char *m,UINT32 l,ACPI_STATUS s,const char *f,...)
{ (void)m;(void)l;(void)s;(void)f; }
static bool inline_completion;
static ACPI_RESOURCE crs[3], aei[2];
#ifdef CIXGPIO_DRIVER_API
static UINT16 event_pin;
static ACPI_INTEGER uid,ngpios,bank,io_mask;
#endif
static bool missing_aei;
static unsigned int aei_calls,factory_mode;
static bool raw_resources;
static UINT8 raw_crs[23], raw_aei[37];
#ifdef CIXGPIO_DRIVER_API
static void test_duplicate(void);
#endif
static void *kmem_alloc(size_t n, int f) { (void)f; void *p=malloc(n); if(!p)abort();live_events++;return p; }
static void kmem_free(void *p, size_t n) { (void)n; assert(live_events);live_events--;free(p); }
static const char *acpi_name(ACPI_HANDLE h) { (void)h; return "test"; }
static void acpi_intr_mask(void *c) { assert(c); masks++; }
static void acpi_intr_unmask(void *c) { assert(c); unmasks++; }
#define device_printf(...) ((void)acpi_name(NULL))
#define aprint_error_dev(...) ((void)acpi_name(NULL))
const char *AcpiFormatException(ACPI_STATUS s) { (void)s; return "test"; }
ACPI_STATUS AcpiGetHandle(ACPI_HANDLE h, const char *p, ACPI_HANDLE *r)
{ (void)h; (void)p; *r=TEST_HANDLE; return AE_OK; }
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h, const char *p,
    ACPI_OBJECT_LIST *o, ACPI_BUFFER *b)
{ (void)h; (void)p; (void)o; (void)b; event_evaluated++; return evaluate_result; }
ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE t, ACPI_OSD_EXEC_CALLBACK f, void *p)
{
    (void)t;
#ifdef CIXGPIO_DRIVER_API
    test_duplicate();
#endif
    if (ACPI_FAILURE(queue_result)) return queue_result;
    if (inline_completion) f(p); else { queued=f; queued_arg=p; }
    return AE_OK;
}
ACPI_STATUS AcpiWalkResources(ACPI_HANDLE h, const char *n,
    ACPI_WALK_RESOURCE_CALLBACK f, void *p)
{
    ACPI_RESOURCE *r;
    unsigned int count;
    (void)h;
    if(raw_resources) {
        bool is_aei=strcmp(n,"_AEI")==0;
        ACPI_RESOURCE *parsed=NULL;
        ACPI_STATUS rv=AcpiBufferToResource(is_aei?raw_aei:raw_crs,
            is_aei?sizeof(raw_aei):sizeof(raw_crs),&parsed);
        if(ACPI_FAILURE(rv))return rv;
        ACPI_RESOURCE *last=parsed;
        ACPI_SIZE length=0;
        for(unsigned int i=0;i<8;i++) {
            length+=last->Length;
            if(last->Type==ACPI_RESOURCE_TYPE_END_TAG)break;
            last=ACPI_NEXT_RESOURCE(last);
        }
        ACPI_BUFFER buffer={length,parsed};
        rv=AcpiWalkResourceBuffer(&buffer,f,p);
        AcpiOsFree(parsed);
        return rv;
    }
    if (strcmp(n,"_AEI")==0) {
        if(missing_aei) return AE_NOT_FOUND;
        aei_calls++;
        if(aei_calls==2 && factory_mode==1)return AE_NO_MEMORY;
        if(aei_calls==2 && factory_mode==2)return AE_OK;
        if(aei_calls==2 && factory_mode==4)event_pin=7;
        if(aei_calls==2 && factory_mode==3) {
            ACPI_STATUS st=f(&aei[0],p);
            if(ACPI_FAILURE(st))return st;
        }
        r=aei;count=2;
    } else { r=crs;count=3; }
    for(unsigned int i=0;i<count;i++) {
        ACPI_STATUS st=f(&r[i],p);
        if(ACPI_FAILURE(st)) return st;
    }
    return AE_OK;
}
struct acpi_irq { unsigned int ar_irq; unsigned char ar_type; };
struct acpi_resources { int unused; };
static int acpi_resource_parse_ops_default;
static ACPI_STATUS acpi_resource_parse(device_t d, ACPI_HANDLE h, const char *n,
    struct acpi_resources *r, const void *o)
{ (void)d;(void)h;(void)n;(void)r;(void)o;return AE_NOT_FOUND; }
static struct acpi_irq *acpi_res_irq(struct acpi_resources *r, int n)
{ (void)r;(void)n;return NULL; }
static void acpi_resource_cleanup(struct acpi_resources *r) { (void)r; }
#include "acpi-event-source.inc"

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL %s:%u: %s\n",__FILE__,(unsigned int)__LINE__,#x); } } while (0)
static void finish(void *p, ACPI_STATUS status)
{ CHECK(p==TEST_ARGUMENT); CHECK(status==evaluate_result || status==queue_result); completed++; }
static void reset(void)
{
    masks=unmasks=event_evaluated=completed=0; queued=NULL; queued_arg=NULL;
    evaluate_result=queue_result=AE_OK; inline_completion=false;
}
static void driver_tests(void);
static void parser_tests(void);
int main(void)
{
    struct acpi_event *ev;
    parser_tests();
    reset();
    CHECK(acpi_event_create(NULL,TEST_HANDLE,6,ACPI_LEVEL_SENSITIVE,&ev)==AE_OK);
    acpi_event_set_intrcookie(ev,TEST_HANDLE);
    /* The old lifecycle must retain its existing unmask-on-AML-error behavior. */
    evaluate_result=AE_ERROR;
    CHECK(acpi_event_notify(ev)==AE_OK); CHECK(masks==1 && unmasks==0);
    queued(queued_arg); CHECK(event_evaluated==1 && unmasks==1);
    kmem_free(ev,sizeof(*ev));
    reset();
    CHECK(acpi_event_create(NULL,TEST_HANDLE,6,ACPI_LEVEL_SENSITIVE,&ev)==AE_OK);
    acpi_event_set_intrcookie(ev,TEST_HANDLE);
#ifdef CIXGPIO_COMPLETION_API
    acpi_event_set_completion(ev,finish,TEST_ARGUMENT);
#else
    /* RED baseline: the existing framework has no opt-in completion hook. */
    (void)finish;
#endif
    evaluate_result=AE_ERROR;
    CHECK(acpi_event_notify(ev)==AE_OK);
    queued(queued_arg);
    CHECK(completed==1); CHECK(unmasks==0);
    queue_result=AE_NO_MEMORY;
    CHECK(acpi_event_notify(ev)==AE_NO_MEMORY);
    CHECK(completed==2 && unmasks==0);
    queue_result=evaluate_result=AE_OK; inline_completion=true;
    CHECK(acpi_event_notify(ev)==AE_OK); CHECK(completed==3 && unmasks==0);
    kmem_free(ev,sizeof(*ev));
    driver_tests();
    printf("CIX GPIO actual-source contract: %u assertions, %u failures\n",checks,failures);
    return failures?1:0;
}

/* Public decoder must reject a header/body that exceeds its input buffer. */
static void parser_tests(void)
{
    static const UINT8 samples[][37] = {
        {0x86,9,0,1,0,0x40,0,0x16,0,0x10,0,0,
         0x89,6,0,1,1,0x94,1,0,0,0x79,0},
        {0x8c,0x20,0,1,0,1,0,2,0,1,0,0,0,0,
         0x17,0,0,0x19,0,0x23,0,0,0,6,0,0x5c,0x5f,0x53,0x42,0x2e,0x47,
         0x50,0x49,0x34,0,0x79,0}
    };
    static const UINT16 lengths[] = {23,37};

    for (unsigned int sample=0;sample<2;sample++) {
        for (UINT16 n=0;n<=lengths[sample];n++) {
            UINT8 *input=malloc(n?n:1);
            ACPI_RESOURCE *resource=NULL;
            ACPI_STATUS expected=AE_AML_BAD_RESOURCE_LENGTH;
            assert(input);
            memcpy(input,samples[sample],n);
            /* The public API allows missing EndTag at whole-record boundaries. */
            if(n==0 || n==lengths[sample] ||
                (sample==0 && (n==12 || n==21)) || (sample==1 && n==35))
                expected=AE_OK;
            ACPI_STATUS status=AcpiBufferToResource(input,n,&resource);
            CHECK(status==expected);
            CHECK((resource!=NULL)==ACPI_SUCCESS(status));
            if(resource)AcpiOsFree(resource);
            /* Also protect callers that walk without the sizing pass. */
            void *end=NULL;
            ACPI_STATUS walk_expected=expected;
            if(n<2 || (expected==AE_OK && n!=lengths[sample]))
                walk_expected=AE_AML_NO_RESOURCE_END_TAG;
            CHECK(AcpiUtWalkAmlResources(NULL,input,n,NULL,&end)==walk_expected);
            free(input);
        }
        UINT8 *input=malloc(lengths[sample]);
        ACPI_RESOURCE *resource=NULL;
        assert(input);
        memcpy(input,samples[sample],lengths[sample]);
        input[1]=0xff;
        CHECK(AcpiBufferToResource(input,lengths[sample],&resource)==AE_AML_BAD_RESOURCE_LENGTH);
        CHECK(resource==NULL);
        void *end=NULL;
        CHECK(AcpiUtWalkAmlResources(NULL,input,lengths[sample],NULL,&end)==AE_AML_BAD_RESOURCE_LENGTH);
        free(input);
    }
}

#ifdef CIXGPIO_DRIVER_API
#include "cixgpio-test-kernel.h"
#include "cixgpio-source.inc"
static struct cixgpio_acpi_softc device;
static struct acpi_devnode node={.ad_handle=TEST_HANDLE};
static struct acpi_attach_args aa={.aa_node=&node};
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h,const char *n,ACPI_INTEGER *v)
{ (void)h;CHECK(strcmp(n,"_UID")==0);*v=uid;return AE_OK; }
static ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE h,const char *n,ACPI_INTEGER *v)
{
    (void)h;
    if(strcmp(n,"ngpios")==0)*v=ngpios;
    else if(strcmp(n,"id")==0)*v=bank;
    else if(strcmp(n,"gpio-io-mask")==0)*v=io_mask;
    else return AE_NOT_FOUND;
    return AE_OK;
}
static void test_duplicate(void)
{
    if(!simulate_duplicate)return;
    simulate_duplicate=false;
    locked_queue=device.sc_lock.held!=0;
    CHECK(!locked_queue);
    CHECK(cixgpio_acpi_intr(&device)==1);
}
static void fixture(int flags)
{
    reset();memset(&device,0,sizeof(device));private_data=&device;cf.cf_flags=flags;
    memset(reads,0,sizeof(reads));memset(writes,0,sizeof(writes));memset(regs,0,sizeof(regs));
    mapped=unmapped=irq_established=irq_disestablished=write_count=last_write=0;
    map_fail=irq_fail=missing_aei=locked_queue=simulate_duplicate=bad_readback=false;
    static const UINT8 saved_crs[23]={0x86,9,0,1,0,0x40,0,0x16,0,0x10,0,0,
        0x89,6,0,1,1,0x94,1,0,0,0x79,0};
    static const UINT8 saved_aei[37]={0x8c,0x20,0,1,0,1,0,2,0,1,0,0,0,0,
        0x17,0,0,0x19,0,0x23,0,0,0,6,0,0x5c,0x5f,0x53,0x42,0x2e,0x47,
        0x50,0x49,0x34,0,0x79,0};
    raw_resources=false;aei_calls=factory_mode=0;
    memcpy(raw_crs,saved_crs,sizeof(raw_crs));memcpy(raw_aei,saved_aei,sizeof(raw_aei));
    uid=4;ngpios=32;bank=io_mask=0;event_pin=6;
    memset(crs,0,sizeof(crs));memset(aei,0,sizeof(aei));
    for(unsigned int i=0;i<3;i++)crs[i].Length=sizeof(ACPI_RESOURCE);
    for(unsigned int i=0;i<2;i++)aei[i].Length=sizeof(ACPI_RESOURCE);
    crs[0].Type=ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
    crs[0].Data.FixedMemory32.WriteProtect=ACPI_READ_WRITE_MEMORY;
    crs[0].Data.FixedMemory32.Address=0x16004000;
    crs[0].Data.FixedMemory32.AddressLength=0x1000;
    crs[1].Type=ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
    crs[1].Data.ExtendedIrq.ProducerConsumer=ACPI_CONSUMER;
    crs[1].Data.ExtendedIrq.Triggering=ACPI_LEVEL_SENSITIVE;
    crs[1].Data.ExtendedIrq.Polarity=ACPI_ACTIVE_HIGH;
    crs[1].Data.ExtendedIrq.Shareable=ACPI_EXCLUSIVE;
    crs[1].Data.ExtendedIrq.InterruptCount=1;
    crs[1].Data.ExtendedIrq.Interrupts[0]=404;
    crs[2].Type=ACPI_RESOURCE_TYPE_END_TAG;
    aei[0].Type=ACPI_RESOURCE_TYPE_GPIO;
    aei[0].Data.Gpio.RevisionId=1;
    aei[0].Data.Gpio.ConnectionType=ACPI_RESOURCE_GPIO_TYPE_INT;
    aei[0].Data.Gpio.ProducerConsumer=ACPI_CONSUMER;
    aei[0].Data.Gpio.Triggering=ACPI_LEVEL_SENSITIVE;
    aei[0].Data.Gpio.Polarity=ACPI_ACTIVE_LOW;
    aei[0].Data.Gpio.PinConfig=ACPI_PIN_CONFIG_PULLUP;
    aei[0].Data.Gpio.PinTableLength=1;
    aei[0].Data.Gpio.PinTable=&event_pin;
    aei[0].Data.Gpio.ResourceSource.StringPtr="\\_SB.GPI4";
    aei[0].Data.Gpio.ResourceSource.StringLength=10;
    aei[1].Type=ACPI_RESOURCE_TYPE_END_TAG;
    regs[1]=UINT32_MAX;regs[4]=0x40;regs[5]=UINT32_MAX;
    regs[9]=0xaabbcc00;regs[10]=0x123456ff;regs[11]=0x555555ff;
}
static void finish_driver(void)
{
    /* Test teardown after all modeled jobs complete; production never detaches. */
    CHECK(!device.sc_pending);
    if(device.sc_event)kmem_free(device.sc_event,sizeof(*device.sc_event));
    CHECK(live_events==0);
}
static void inject_irq(void)
{ regs[8]=0x40;CHECK(cixgpio_acpi_intr(&device)==1); }
static void driver_tests(void)
{
    for(unsigned int mode=0;mode<2;mode++) {
        fixture((int)mode);raw_resources=true;
        CHECK(cixgpio_acpi_match(NULL,&cf,&aa)>0);
        cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
        CHECK(mapped==1 && irq_established==mode);finish_driver();
    }
    for(unsigned int bad=0;bad<6;bad++) {
        fixture(1);raw_resources=true;
        if(bad==0)raw_crs[3]=0; /* Firmware declares read-only memory. */
        if(bad==1)raw_crs[0]=0x8b; /* Unsupported Address64, never silently dropped. */
        if(bad==2)raw_aei[23]=32;
        if(bad==3)raw_aei[7]=3; /* Edge instead of level. */
        if(bad==4)raw_aei[33]='5'; /* Foreign resource source. */
        if(bad==5)raw_aei[1]=0xff; /* Malformed descriptor length. */
        cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
        CHECK(mapped==0 && write_count==0 && irq_established==0);finish_driver();
    }
    fixture(0);
    CHECK(cixgpio_acpi_match(NULL,&cf,&aa)>0);
    cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    CHECK(mapped==1 && irq_established==0 && write_count==0 && event_evaluated==0);
    CHECK(reads[8]==0 && reads[0]==1 && reads[11]==1);
    finish_driver();
    for(unsigned int bad=0;bad<18;bad++) {
        fixture(1);
        switch(bad) {
        case 0:missing_aei=true;break;
        case 1:event_pin=32;break;
        case 2:aei[0].Data.Gpio.Polarity=ACPI_ACTIVE_HIGH;break;
        case 3:aei[0].Data.Gpio.PinTableLength=2;break;
        case 4:crs[0].Data.FixedMemory32.Address=UINT32_MAX;break;
        case 5:crs[0].Data.FixedMemory32.AddressLength=0;break;
        case 6:crs[1].Data.ExtendedIrq.InterruptCount=0;break;
        case 7:crs[1].Data.ExtendedIrq.Shareable=ACPI_SHARED;break;
        case 8:crs[0].Type=ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64;break;
        case 9:uid=3;break;
        case 10:ngpios=0;break;
        case 11:bank=1;break;
        case 12:cf.cf_flags=2;break;
        case 13:crs[1].Data.ExtendedIrq.Polarity=ACPI_ACTIVE_LOW;break;
        case 14:crs[1].Data.ExtendedIrq.Interrupts[0]=403;break;
        case 15:aei[0].Data.Gpio.VendorLength=1;break;
        case 16:aei[0].Data.Gpio.DebounceTimeout=1;break;
        case 17:io_mask=1;break;
        }
        cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
        CHECK(mapped==0 && write_count==0 && irq_established==0);
        finish_driver();
    }
    for(unsigned int bad=0;bad<3;bad++) {
        fixture(1);
        if(bad==0)regs[0]=0x40;
        if(bad==1)regs[1]&=~0x40U;
        if(bad==2)regs[5]&=~0x20U;
        cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
        CHECK(mapped==1 && unmapped==1 && write_count==0 && irq_established==0);
        CHECK(reads[8]==0);
        finish_driver();
    }
    fixture(1);cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    CHECK(device.sc_event && irq_established==1 && last_write==0x18);
    CHECK(regs[9]==(0xaabbcc00|0x40) && regs[10]==(0x123456ff&~0x40U));
    CHECK(regs[11]==(0x555555ff&~0x40U) && writes[0]==0 && writes[1]==0 && writes[2]==0 && writes[3]==0);
    CHECK(reads[8]==0);
    simulate_duplicate=true;inject_irq();
    CHECK(reads[8]==1 && masks==1 && unmasks==0 && device.sc_pending);
    CHECK(cixgpio_acpi_intr(&device)==1 && reads[8]==1);
    CHECK(event_evaluated==0);queued(queued_arg);
    CHECK(event_evaluated==1 && unmasks==1 && !device.sc_pending && !device.sc_fault);
    queue_result=AE_NO_MEMORY;inject_irq();
    CHECK(device.sc_fault && !device.sc_pending && unmasks==1);
    CHECK(cixgpio_acpi_intr(&device)==1 && reads[8]==2);
    finish_driver();
    fixture(1);cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    evaluate_result=AE_AML_LOOP_TIMEOUT;inject_irq();queued(queued_arg);
    CHECK(device.sc_fault && unmasks==0 && !device.sc_pending);finish_driver();
    fixture(1);cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);inline_completion=true;
    inject_irq();CHECK(!device.sc_pending && unmasks==1 && !device.sc_fault);
    regs[4]=0;
    for(unsigned int i=0;i<31;i++){inject_irq();CHECK(!device.sc_fault);}
    regs[4]=0x40;inject_irq();CHECK(!device.sc_fault);regs[4]=0;
    for(unsigned int i=0;i<31;i++){inject_irq();CHECK(!device.sc_fault);}
    unsigned int before=unmasks;inject_irq();
    CHECK(device.sc_fault && unmasks==before);finish_driver();
    for(unsigned int mode=1;mode<=4;mode++) {
        fixture(1);factory_mode=mode;cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
        CHECK(device.sc_event==NULL && irq_established==0 && unmapped==1);
        CHECK(writes[6]==0);finish_driver();
    }
    fixture(1);bad_readback=true;cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    CHECK(irq_established==0 && device.sc_event==NULL && unmapped==1);
    CHECK(writes[6]==0 && (regs[5]&0x40)!=0);finish_driver();
    fixture(1);map_fail=true;cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    CHECK(mapped==1 && write_count==0 && irq_established==0);finish_driver();
    fixture(1);irq_fail=true;cixgpio_acpi_attach(NULL,TEST_HANDLE,&aa);
    CHECK(irq_established==1 && device.sc_event==NULL && unmapped==1);
    CHECK((regs[5]&0x40)!=0);finish_driver();
    (void)test_attach_refs;
}
#else
static void driver_tests(void)
{ CHECK(!"CIXH1003 GPIO event driver is absent"); }
#endif
