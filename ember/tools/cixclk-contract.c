/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source CIX clock contracts, AI-assisted. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define ACPI_APPLICATION
#include <acpi.h>

#ifndef CIXCLK_DRIVER_API
int main(void)
{
    fputs("FAIL: no CIX clock provider can claim the firmware clock\n",stderr);
    return 1;
}
#else
static atomic_uint checks, failures, calls, live, diagnostics;
static char diagnostic[256];
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL line %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
typedef void *device_t;
typedef void *cfdata_t;
typedef struct { pthread_mutex_t lock; bool held; } kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KASSERT assert
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL {NULL}
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void * const test_attach_refs[] = {(const void *)(m),(const void *)(a)}
struct acpi_devnode { ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; };
static void *private_data;
static ACPI_INTEGER test_uid;
static int match_result=1;
static bool uid_fail, intr;
static _Thread_local unsigned int mode;
static _Thread_local bool expect_enable;
static _Thread_local const char *expect_method="GCLK";
static _Thread_local uint32_t expect_id=0x43;
static char provider_token, other_token;
#define HANDLE ((void *)&provider_token)
#define OTHER ((void *)&other_token)
static void mutex_init(kmutex_t *m,int a,int b)
{ (void)a;(void)b;assert(pthread_mutex_init(&m->lock,NULL)==0);m->held=false; }
static void mutex_enter(kmutex_t *m)
{ assert(pthread_mutex_lock(&m->lock)==0);assert(!m->held);m->held=true; }
static void mutex_exit(kmutex_t *m)
{ assert(m->held);m->held=false;assert(pthread_mutex_unlock(&m->lock)==0); }
static bool cpu_intr_p(void) { return intr; }
static bool cpu_softintr_p(void) { return intr; }
static void *device_private(device_t d) { (void)d;return private_data; }
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
static void test_print(device_t d,const char *fmt,...)
{
    va_list ap;
    CHECK(d==HANDLE || d==OTHER);
    diagnostics++;
    va_start(ap,fmt);
    (void)vsnprintf(diagnostic,sizeof(diagnostic),fmt,ap);
    va_end(ap);
}
#define aprint_error_dev test_print
const char *AcpiFormatException(ACPI_STATUS s)
{ CHECK(s==AE_ERROR);return "AE_ERROR"; }
static int acpi_compatible_match(const void *a,const struct device_compatible_entry *e)
{ (void)a;CHECK(strcmp(e[0].compat,"CIXHA010")==0);return match_result; }
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h,const char *n,ACPI_INTEGER *v)
{ CHECK(h==HANDLE);CHECK(strcmp(n,"_UID")==0);if(uid_fail)return AE_ERROR;*v=test_uid;return AE_OK; }
void AcpiOsFree(void *p) { assert(live);live--;free(p); }
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h,const char *n,ACPI_OBJECT_LIST *a,ACPI_BUFFER *b);
#include "cixclk-source.inc"

/* ACPICA and firmware boundaries only. Every provider function above is intact. */
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h,const char *n,ACPI_OBJECT_LIST *a,ACPI_BUFFER *b)
{
    ACPI_OBJECT *o;
    unsigned char *data;
    calls++;
    CHECK(cixclk_provider->sc_lock.held);
    CHECK(h==HANDLE);
    CHECK(strcmp(n,expect_method)==0);
    CHECK(a->Count==(strcmp(n,"GCLK")==0?1u:2u));
    CHECK(a->Pointer[0].Type==ACPI_TYPE_INTEGER && a->Pointer[0].Integer.Value==expect_id);
    if(a->Count==2) CHECK(a->Pointer[1].Type==ACPI_TYPE_INTEGER &&
        a->Pointer[1].Integer.Value==(expect_enable?1u:0u));
    CHECK(b->Length==ACPI_ALLOCATE_BUFFER && b->Pointer==NULL);
    if(mode==1)return AE_ERROR;
    if(mode==2){b->Length=0;return AE_OK;}
    o=calloc(1,sizeof(*o)+97);assert(o);live++;
    b->Pointer=o;b->Length=sizeof(*o)+97;
    data=(unsigned char *)(o+1)+1; /* Force unaligned firmware payload. */
    o->Type=ACPI_TYPE_BUFFER;o->Buffer.Pointer=data;o->Buffer.Length=96;
    data[4]=0x78;data[5]=0x56;data[6]=0x34;data[7]=0x12;
    data[8]=0xef;data[9]=0xcd;data[10]=0xab;data[11]=0x90;
    switch(mode){
    case 3:o->Type=ACPI_TYPE_INTEGER;break;
    case 4:o->Buffer.Pointer=NULL;break;
    case 5:o->Buffer.Length=3;break;
    case 6:o->Buffer.Length=4;data[0]=6;break;
    case 7:o->Buffer.Length=4;data[0]=11;break;
    case 8:o->Buffer.Length=11;break;
    case 9:b->Length=sizeof(*o)-1;break;
    case 10:o->Buffer.Length=97;break;
    case 11:b->Length=0;break;
    case 13:o->Buffer.Length=4;break;
    case 14:o->Buffer.Length=12;break;
    case 15:o->Buffer.Pointer=(void *)((uintptr_t)o-1);break;
    case 16:o->Buffer.Pointer=(void *)((uintptr_t)o+sizeof(*o)+98);break;
    case 17:o->Buffer.Length=UINT32_MAX;break;
    case 18:b->Length=sizeof(*o);break;
    case 19:data[3]=1;break;
    case 20:data[0]=0xfc;data[1]=0xff;data[2]=0xff;data[3]=0xff;break;
    case 12:return AE_ERROR; /* ACPICA may return an allocation on failure. */
    default:break;
    }
    return AE_OK;
}

/* Changing argument forwarding, per-ID ownership or read-only policy breaks these. */
struct rate_worker {
    struct cixclk *clock;
    uint32_t id;
};

static void *
rate_thread(void *arg)
{
    const struct rate_worker *worker=arg;
    uint64_t rate;

    expect_id=worker->id;
    for (unsigned int i=0;i<4000;i++) {
        rate=UINT64_C(0xfeedface);
        CHECK(cixclk_acpi_get_rate(worker->clock,&rate)==0);
        CHECK(rate==UINT64_C(0x90abcdef12345678));
    }
    return NULL;
}

static void
rate_only_tests(void)
{
    const uint32_t rejected[]={0,0x42,247,249,263,265,267,271,UINT32_MAX};
    struct cixclk sentinel_storage={0};
    struct cixclk *sentinel=&sentinel_storage, *out;
    struct cixclk *uart1=NULL, *uart3=NULL, *gate=NULL;
    const unsigned int bad_responses[]={1,2,3,4,5,6,7,8,9,10,11,12,13,15,16,17,18,19,20};
    pthread_t first,second;
    struct rate_worker workers[2];
    uint64_t rate;
    unsigned int before;

    for (unsigned int i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++) {
        out=sentinel;
        CHECK(cixclk_acpi_get(HANDLE,rejected[i],&out)==ENOTSUP && out==sentinel);
    }
    CHECK(cixclk_acpi_get(HANDLE,264,&uart1)==0);
    CHECK(cixclk_acpi_get(HANDLE,266,&uart3)==0);
    if (uart1==NULL || uart3==NULL) {
        if (uart1!=NULL)CHECK(cixclk_acpi_put(uart1)==0);
        if (uart3!=NULL)CHECK(cixclk_acpi_put(uart3)==0);
        return; /* Baseline RED reports missing claims without a null call. */
    }
    CHECK(cixclk_acpi_get(HANDLE,67,&gate)==0);
    CHECK(uart1!=uart3 && uart1!=gate && uart3!=gate);
    out=sentinel;CHECK(cixclk_acpi_get(HANDLE,264,&out)==EBUSY && out==sentinel);
    out=sentinel;CHECK(cixclk_acpi_get(HANDLE,266,&out)==EBUSY && out==sentinel);
    out=sentinel;CHECK(cixclk_acpi_get(HANDLE,67,&out)==EBUSY && out==sentinel);
    before=calls;
    CHECK(cixclk_acpi_enable(uart1)==ENOTSUP);
    CHECK(cixclk_acpi_disable(uart1)==ENOTSUP);
    CHECK(cixclk_acpi_enable(uart3)==ENOTSUP);
    CHECK(cixclk_acpi_disable(uart3)==ENOTSUP);
    CHECK(calls==before);

    struct cixclk *const clocks[]={uart1,uart3,gate};
    const uint32_t ids[]={264,266,67};
    for (unsigned int i=0;i<3;i++) {
        expect_id=ids[i];expect_method="GCLK";mode=14;
        CHECK(cixclk_acpi_get_rate(clocks[i],&rate)==0);
        CHECK(rate==UINT64_C(0x90abcdef12345678));
        for (unsigned int j=0;j<sizeof(bad_responses)/sizeof(bad_responses[0]);j++) {
            mode=bad_responses[j];rate=UINT64_C(0xfeedface);
            CHECK(cixclk_acpi_get_rate(clocks[i],&rate)!=0);
            CHECK(rate==UINT64_C(0xfeedface) && live==0);
            if (mode==20)CHECK(strstr(diagnostic,"clock status 4294967292")!=NULL);
        }
    }
    /* Gate uncertainty must not block an unrelated read-only claim's lifetime. */
    expect_id=67;expect_method="CLKC";expect_enable=true;mode=6;
    CHECK(cixclk_acpi_enable(gate)!=0);
    CHECK(cixclk_acpi_put(gate)==EBUSY);
    CHECK(cixclk_acpi_put(uart3)==0);
    CHECK(cixclk_acpi_get(HANDLE,266,&uart3)==0);
    expect_id=264;expect_method="GCLK";mode=0;
    CHECK(cixclk_acpi_get_rate(uart1,&rate)==0);
    CHECK(cixclk_acpi_put(uart1)==0);
    out=sentinel;CHECK(cixclk_acpi_get(HANDLE,266,&out)==EBUSY && out==sentinel);
    CHECK(cixclk_acpi_get(HANDLE,264,&uart1)==0);
    expect_id=67;expect_method="CLKC";expect_enable=false;
    CHECK(cixclk_acpi_disable(gate)==0);
    CHECK(cixclk_acpi_put(gate)==0);

    /* Real host mutex, independent actual-source claims, serialized AML calls. */
    workers[0]=(struct rate_worker){uart1,264};
    workers[1]=(struct rate_worker){uart3,266};
    alarm(30);
    assert(pthread_create(&first,NULL,rate_thread,&workers[0])==0);
    assert(pthread_create(&second,NULL,rate_thread,&workers[1])==0);
    assert(pthread_join(first,NULL)==0);
    assert(pthread_join(second,NULL)==0);
    alarm(0);
    CHECK(cixclk_acpi_put(uart1)==0);
    CHECK(cixclk_acpi_get(HANDLE,264,&uart1)==0);
    expect_id=266;expect_method="GCLK";mode=0;
    CHECK(cixclk_acpi_get_rate(uart3,&rate)==0);
    CHECK(cixclk_acpi_put(uart3)==0);
    CHECK(cixclk_acpi_put(uart1)==0);
    before=calls;
    CHECK(cixclk_acpi_enable(uart1)==EINVAL);
    CHECK(cixclk_acpi_disable(uart1)==EINVAL);
    CHECK(calls==before);
}

int main(void)
{
    struct cixclk_acpi_softc sc={0}, duplicate={0};
    struct acpi_devnode node={HANDLE};
    struct acpi_attach_args aa={&node};
    struct cixclk sentinel_storage={0};
    struct cixclk *clk=NULL, *sentinel=&sentinel_storage, *out;
    uint64_t rate;
    unsigned int before;
    CHECK(test_attach_refs[0]!=NULL);
    out=sentinel;
    CHECK(cixclk_acpi_get(HANDLE,0x43,&out)==ENXIO && out==sentinel);
    CHECK(cixclk_acpi_match(NULL,NULL,&aa)!=0);
    test_uid=1;CHECK(cixclk_acpi_match(NULL,NULL,&aa)==0);test_uid=0;
    match_result=0;CHECK(cixclk_acpi_match(NULL,NULL,&aa)==0);match_result=1;
    uid_fail=true;CHECK(cixclk_acpi_match(NULL,NULL,&aa)==0);uid_fail=false;
    private_data=&sc;
    cixclk_acpi_attach(NULL,HANDLE,&aa);
    CHECK(calls==0 && live==0); /* Adding a probe at attach breaks this. */
    private_data=&duplicate;cixclk_acpi_attach(NULL,OTHER,&aa);
    CHECK(calls==0);
    CHECK(cixclk_acpi_get(OTHER,0x43,&out)==ENXIO && out==sentinel);
    CHECK(cixclk_acpi_get(HANDLE,0x42,&out)==ENOTSUP && out==sentinel);
    CHECK(cixclk_acpi_get(HANDLE,0x43,NULL)==EINVAL);
    CHECK(cixclk_acpi_get(HANDLE,0x43,&clk)==0 && clk!=NULL);
    CHECK(cixclk_acpi_get(HANDLE,0x43,&out)==EBUSY && out==sentinel);
    CHECK(cixclk_acpi_disable(clk)==EALREADY && calls==0);
    expect_method="GCLK";
    for(mode=1;mode<=12;mode++) {
        before=diagnostics;rate=UINT64_C(0xfeedface);
        CHECK(cixclk_acpi_get_rate(clk,&rate)!=0 && rate==UINT64_C(0xfeedface));
        CHECK(diagnostics==before+1);
        CHECK(live==0);
        CHECK(strstr(diagnostic,"GCLK")!=NULL);
        if(mode==1 || mode==12)CHECK(strstr(diagnostic,"AE_ERROR")!=NULL);
        else if(mode==6)CHECK(strstr(diagnostic,"clock status 6")!=NULL);
        else if(mode==7)CHECK(strstr(diagnostic,"clock status 11")!=NULL);
        else CHECK(strstr(diagnostic,"invalid clock response")!=NULL);
    }
    for(mode=15;mode<=19;mode++) {
        rate=UINT64_C(0xfeedface);
        CHECK(cixclk_acpi_get_rate(clk,&rate)!=0 && rate==UINT64_C(0xfeedface));
        CHECK(live==0);
    }
    mode=14;CHECK(cixclk_acpi_get_rate(clk,&rate)==0 && rate==UINT64_C(0x90abcdef12345678));
    mode=0;CHECK(cixclk_acpi_get_rate(clk,&rate)==0 && rate==UINT64_C(0x90abcdef12345678));
    CHECK(cixclk_acpi_get_rate(clk,NULL)==EINVAL);
    expect_method="CLKC";expect_enable=true;
    mode=6;CHECK(cixclk_acpi_enable(clk)!=0);
    CHECK(cixclk_acpi_put(clk)==EBUSY);
    CHECK(cixclk_acpi_get(HANDLE,0x43,&out)==EBUSY && out==sentinel);
    before=calls;CHECK(cixclk_acpi_enable(clk)==EIO && calls==before);
    expect_enable=false;mode=0;CHECK(cixclk_acpi_disable(clk)==0);
    /* Each failure leaves a claim until an explicit successful rollback. */
    for(unsigned int bad=1;bad<=19;bad++) {
        if(bad==8 || bad==13 || bad==14)continue; /* Eleven bytes is valid for CLKC. */
        mode=bad;expect_enable=true;before=diagnostics;
        CHECK(cixclk_acpi_enable(clk)!=0);
        CHECK(diagnostics==before+1 && strstr(diagnostic,"CLKC")!=NULL);
        if(bad==1 || bad==12)CHECK(strstr(diagnostic,"AE_ERROR")!=NULL);
        else if(bad==6)CHECK(strstr(diagnostic,"clock status 6")!=NULL);
        else if(bad==7)CHECK(strstr(diagnostic,"clock status 11")!=NULL);
        else if(bad==19)CHECK(strstr(diagnostic,"clock status 16777216")!=NULL);
        else CHECK(strstr(diagnostic,"invalid clock response")!=NULL);
        CHECK(cixclk_acpi_put(clk)==EBUSY);
        expect_enable=false;CHECK(cixclk_acpi_disable(clk)!=0);
        CHECK(cixclk_acpi_put(clk)==EBUSY);
        mode=0;CHECK(cixclk_acpi_disable(clk)==0);
    }
    mode=13;expect_enable=true;CHECK(cixclk_acpi_enable(clk)==0);
    before=calls;CHECK(cixclk_acpi_enable(clk)==EALREADY && calls==before);
    CHECK(cixclk_acpi_put(clk)==EBUSY);
    mode=6;expect_enable=false;CHECK(cixclk_acpi_disable(clk)!=0);
    CHECK(cixclk_acpi_put(clk)==EBUSY);
    mode=0;CHECK(cixclk_acpi_disable(clk)==0);
    before=calls;CHECK(cixclk_acpi_disable(clk)==EALREADY && calls==before);
    CHECK(cixclk_acpi_put(clk)==0);
    CHECK(cixclk_acpi_put(clk)==EINVAL);
    before=calls;CHECK(cixclk_acpi_enable(clk)==EINVAL && calls==before);
    CHECK(cixclk_acpi_get_rate(clk,&rate)==EINVAL);
    CHECK(cixclk_acpi_get(HANDLE,0x43,&clk)==0);
    intr=true;CHECK(cixclk_acpi_enable(clk)==EWOULDBLOCK);
    CHECK(cixclk_acpi_disable(clk)==EWOULDBLOCK);
    out=sentinel;CHECK(cixclk_acpi_get(HANDLE,0x43,&out)==EWOULDBLOCK && out==sentinel);
    CHECK(cixclk_acpi_get_rate(clk,&rate)==EWOULDBLOCK);
    CHECK(cixclk_acpi_put(clk)==EWOULDBLOCK);intr=false;
    CHECK(cixclk_acpi_put(clk)==0);
    CHECK(cixclk_acpi_enable(NULL)==EINVAL);
    CHECK(cixclk_acpi_disable(NULL)==EINVAL);
    CHECK(cixclk_acpi_put(NULL)==EINVAL);
    CHECK(cixclk_acpi_get_rate(NULL,&rate)==EINVAL);
    CHECK(live==0);
    rate_only_tests();
    CHECK(pthread_mutex_destroy(&sc.sc_lock.lock)==0);
    printf("CIX clock actual-source contract: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
#endif
