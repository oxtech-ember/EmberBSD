/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source CIX SCMI binding contracts, AI-assisted. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#define ACPI_APPLICATION
#include <acpi.h>
#include <dev/ic/scmi_smt_diag.h>

#ifndef CIXSCMI_DRIVER_API
int main(void)
{
    fputs("FAIL: no checked one-request CIX SCMI ACPI binding\n",stderr);
    return 1;
}
#else
static unsigned int checks, failures, evaluations, allocations, maps, unmaps;
static unsigned int reads, writes, doorbells, barriers, completions, pauses;
static unsigned int malformed, failed_node, map_fail, response_mode;
static bool intr, foreign_on_failure, sent, published, ordered, acquired;
static uint64_t ticks;
static uint32_t response_version, response_firmware_status, expected_header;
static uint32_t tx[0x10000/4],rx[0x10000/4];
static int cold=1;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL line %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
#define KASSERT assert
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
typedef struct device *device_t;
struct cfdata { unsigned int cf_flags; };
typedef struct cfdata *cfdata_t;
struct device { void *private; struct cfdata cf; };
typedef struct { pthread_mutex_t lock; bool held; } kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define BUS_SPACE_MAP_NONPOSTED 0x40
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
typedef void *bus_space_tag_t;
typedef uint32_t *bus_space_handle_t;
typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
struct acpi_devnode {
    device_t ad_device,ad_root;
    ACPI_DEVICE_INFO *ad_devinfo;
    ACPI_HANDLE ad_handle;
    uint32_t ad_type;
    struct acpi_devnode *next;
};
struct acpi_softc { struct { struct acpi_devnode *first; } sc_head; };
#define SIMPLEQ_FOREACH(v,h,f) for ((v)=(h)->first;(v)!=NULL;(v)=(v)->next)
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL {NULL}
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void * const test_attach_refs[] = {(const void *)(m),(const void *)(a)}
static struct device rootdev,selfdev,otherdev;
static struct acpi_softc root;
static struct acpi_devnode nodes[5];
static void (*deferred)(device_t);
static unsigned int defers;
static char diagnostic[512];
static void *device_private(device_t d) { return d->private; }
static cfdata_t device_cfdata(device_t d) { return &d->cf; }
static void config_defer(device_t d,void (*f)(device_t))
{ CHECK(d==&selfdev);defers++;deferred=f; }
static void mutex_init(kmutex_t *m,int a,int b)
{ (void)a;(void)b;assert(pthread_mutex_init(&m->lock,NULL)==0);m->held=false; }
static void mutex_enter(kmutex_t *m)
{ assert(pthread_mutex_lock(&m->lock)==0);assert(!m->held);m->held=true; }
static void mutex_exit(kmutex_t *m)
{ assert(m->held);m->held=false;assert(pthread_mutex_unlock(&m->lock)==0); }
static bool mutex_owned(kmutex_t *m) { return m->held; }
static bool cpu_intr_p(void) { return intr; }
static bool cpu_softintr_p(void) { return intr; }
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
static void test_print(device_t d,const char *fmt,...)
{ va_list ap;CHECK(d==&selfdev);va_start(ap,fmt);
    (void)vsnprintf(diagnostic,sizeof(diagnostic),fmt,ap);va_end(ap); }
#define aprint_error_dev test_print
#define aprint_normal_dev test_print
static int acpi_compatible_match(const void *a,const struct device_compatible_entry *e)
{ (void)a;CHECK(strcmp(e[0].compat,"CIXHA006")==0);return 1; }
static unsigned int node_index(ACPI_HANDLE h)
{ for(unsigned int i=0;i<5;i++)if(h==nodes[i].ad_handle)return i;abort(); }
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h,const char *n,ACPI_INTEGER *v)
{
    unsigned int i=node_index(h);evaluations++;
    CHECK(strcmp(n,"_STA")==0 || (i>=3 && strcmp(n,"_CCA")==0));
    if(malformed==1 && i==failed_node)return AE_ERROR;
    *v=strcmp(n,"_STA")==0?0xb:0;
    if(malformed==2 && i==failed_node)*v=0;
    if(malformed==3 && i==failed_node && strcmp(n,"_CCA")==0)*v=1;
    return AE_OK;
}
void AcpiOsFree(void *p) { CHECK(allocations>0);allocations--;free(p); }
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE,const char *,ACPI_OBJECT_LIST *,ACPI_BUFFER *);
ACPI_STATUS AcpiGetCurrentResources(ACPI_HANDLE,ACPI_BUFFER *);
static int bus_space_map(bus_space_tag_t,bus_addr_t,bus_size_t,int,bus_space_handle_t *);
static void bus_space_unmap(bus_space_tag_t,bus_space_handle_t,bus_size_t);
static uint32_t bus_space_read_4(bus_space_tag_t,bus_space_handle_t,bus_size_t);
static void bus_space_write_4(bus_space_tag_t,bus_space_handle_t,bus_size_t,uint32_t);
static void bus_space_barrier(bus_space_tag_t,bus_space_handle_t,bus_size_t,bus_size_t,int);
static void test_dsb(void);
#define dsb(x) test_dsb()
static void getmicrouptime(struct timeval *v)
{ v->tv_sec=(long)(ticks/1000000);v->tv_usec=(int)(ticks%1000000); }
static void delay(unsigned int us) { CHECK(us==10);ticks+=us;pauses++; }
#include "cixscmi-source.inc"

/* Only ACPICA allocation/firmware, autoconfiguration and bus boundaries follow. */
struct info_blob { ACPI_DEVICE_INFO *p; char *hid, *uid; };
static struct info_blob info[5];
struct dsd_blob { ACPI_OBJECT o[32]; unsigned char bytes[256]; };
static const uint8_t uuid[16]={0x14,0xd8,0xff,0xda,0xba,0x6e,0x8c,0x4d,
    0x8a,0x91,0xbc,0x9b,0xbf,0x4a,0xa3,0x01};
static void string_object(struct dsd_blob *b,unsigned int o,unsigned int pos,const char *s)
{ size_t n=strlen(s);memcpy(b->bytes+pos,s,n+1);b->o[o].Type=ACPI_TYPE_STRING;
    b->o[o].String.Pointer=(char *)b->bytes+pos;b->o[o].String.Length=(UINT32)n; }
static void package_object(struct dsd_blob *b,unsigned int o,unsigned int start,unsigned int n)
{ b->o[o].Type=ACPI_TYPE_PACKAGE;b->o[o].Package.Elements=&b->o[start];b->o[o].Package.Count=n; }
static void integer_object(struct dsd_blob *b,unsigned int o,uint64_t n)
{ b->o[o].Type=ACPI_TYPE_INTEGER;b->o[o].Integer.Value=n; }
static void reference_object(struct dsd_blob *b,unsigned int o,unsigned int n)
{ b->o[o].Type=ACPI_TYPE_LOCAL_REFERENCE;b->o[o].Reference.ActualType=ACPI_TYPE_DEVICE;
    b->o[o].Reference.Handle=nodes[n].ad_handle; }
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h,const char *n,ACPI_OBJECT_LIST *a,ACPI_BUFFER *out)
{
    unsigned int i=node_index(h);
    struct dsd_blob *b=calloc(1,sizeof(*b));assert(b);allocations++;evaluations++;
    CHECK(strcmp(n,"_DSD")==0 && a==NULL);
    CHECK(out->Pointer==NULL && out->Length==ACPI_ALLOCATE_BUFFER);
    out->Pointer=b;out->Length=sizeof(*b);
    package_object(b,0,1,2);b->o[1].Type=ACPI_TYPE_BUFFER;
    memcpy(b->bytes,uuid,16);b->o[1].Buffer.Pointer=b->bytes;b->o[1].Buffer.Length=16;
    package_object(b,2,3,i<3?2:1);package_object(b,3,5,2);
    if(i<3)package_object(b,4,7,2);
    if(i==0){
        string_object(b,5,32,"mboxes");package_object(b,6,9,4);
        reference_object(b,9,1);integer_object(b,10,8);
        reference_object(b,11,2);integer_object(b,12,8);
        string_object(b,7,64,"shmem");package_object(b,8,13,2);
        reference_object(b,13,3);reference_object(b,14,4);
    }else if(i<3){
        string_object(b,5,32,"cix,mbox_dir");integer_object(b,6,i-1);
        string_object(b,7,64,"#mbox-cells");integer_object(b,8,1);
    }else{
        string_object(b,5,32,"compatible");string_object(b,6,64,"arm,scmi-shmem");
    }
    if(i!=failed_node)return AE_OK;
    switch(malformed){
    case 4:return AE_ERROR;
    case 5:out->Length=sizeof(ACPI_OBJECT)-1;break;
    case 6:b->o[0].Type=ACPI_TYPE_INTEGER;break;
    case 7:b->o[0].Package.Elements=NULL;break;
    case 8:b->o[0].Package.Count=UINT32_MAX;break;
    case 9:b->o[1].Buffer.Pointer=(void *)((uintptr_t)b-1);break;
    case 10:b->bytes[0]^=1;break;
    case 11:b->o[2].Package.Count++;break;
    case 12:b->o[3].Package.Elements=(void *)((uintptr_t)b+sizeof(*b)-1);break;
    case 13:b->o[5].String.Pointer=NULL;break;
    case 14:b->o[5].String.Length=UINT32_MAX;break;
    case 15:b->bytes[32+b->o[5].String.Length]='x';break;
    case 16:if(i<3)b->o[7]=b->o[5];else b->o[6].String.Length--;break;
    case 17:b->o[6].Type=ACPI_TYPE_BUFFER;break;
    case 18:if(i==0)integer_object(b,10,7);else integer_object(b,6,9);break;
    case 19:if(i==0)b->o[9].Reference.Handle=(void *)&otherdev;break;
    case 20:if(i==0)b->o[11]=b->o[9];break;
    case 21:if(i==0)b->o[13]=b->o[9];break;
    case 22:b->o[1].Buffer.Length=15;break;
    case 23:b->o[0].Package.Elements=(void *)(UINTPTR_MAX-3);break;
    case 24:if(i==0)b->o[9].Reference.ActualType=ACPI_TYPE_INTEGER;break;
    case 25:out->Length=0;break;
    case 26:out->Length=SIZE_MAX;break;
    case 27:if(i==0)b->o[9].Reference.Handle=NULL;else b->o[5].String.Pointer=(void *)(UINTPTR_MAX-1);break;
    case 28:if(i==0)b->o[13].Reference.Handle=nodes[0].ad_handle;else b->o[5].String.Pointer=(void *)((uintptr_t)b+sizeof(*b));break;
    case 29:if(i==0)b->o[8].Package.Count=1;else b->o[3].Package.Count=1;break;
    case 44:b->o[1].Buffer.Pointer=NULL;break;
    case 45:b->o[2].Package.Elements=NULL;break;
    case 46:b->o[5].String.Pointer=(void *)((uintptr_t)b+sizeof(*b)-1);break;
    default:break;
    }
    return AE_OK;
}
ACPI_STATUS AcpiGetCurrentResources(ACPI_HANDLE h,ACPI_BUFFER *out)
{
    unsigned int i=node_index(h);size_t len=ACPI_RS_SIZE(ACPI_RESOURCE_FIXED_MEMORY32);
    size_t irq=ACPI_RS_SIZE(ACPI_RESOURCE_EXTENDED_IRQ),end=ACPI_RS_SIZE(ACPI_RESOURCE_END_TAG);
    unsigned char *p=calloc(1,len+irq+end+8);assert(p);allocations++;evaluations++;
    ACPI_RESOURCE r;memset(&r,0,sizeof(r));
    r.Type=ACPI_RESOURCE_TYPE_FIXED_MEMORY32;r.Length=(UINT32)len;
    r.Data.FixedMemory32.WriteProtect=ACPI_READ_WRITE_MEMORY;
    r.Data.FixedMemory32.Address=(i==1 || i==3)?0x06590000:0x065a0000;
    r.Data.FixedMemory32.AddressLength=i<3?0x10000:0x80;
    memcpy(p,&r,len);
    if(i<3){memset(&r,0,sizeof(r));r.Type=ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
        r.Length=(UINT32)irq;r.Data.ExtendedIrq.ProducerConsumer=ACPI_CONSUMER;
        r.Data.ExtendedIrq.Triggering=ACPI_LEVEL_SENSITIVE;
        r.Data.ExtendedIrq.Polarity=ACPI_ACTIVE_HIGH;r.Data.ExtendedIrq.Shareable=ACPI_EXCLUSIVE;
        r.Data.ExtendedIrq.InterruptCount=1;r.Data.ExtendedIrq.Interrupts[0]=i==1?395:391;
        memcpy(p+len,&r,irq);
    }else irq=0;
    memset(&r,0,sizeof(r));r.Type=ACPI_RESOURCE_TYPE_END_TAG;r.Length=(UINT32)end;
    memcpy(p+len+irq,&r,end);out->Pointer=p;out->Length=len+irq+end;
    if(i!=failed_node)return AE_OK;
    uint32_t value;
    switch(malformed){
    case 30:return AE_ERROR;
    case 31:out->Length=7;break;
    case 32:value=0;memcpy(p+4,&value,4);break;
    case 33:value=UINT32_MAX;memcpy(p+4,&value,4);break;
    case 34:value=ACPI_RESOURCE_TYPE_IO;memcpy(p,&value,4);break;
    case 35:p[8]=ACPI_READ_ONLY_MEMORY;break;
    case 36:value=0x065d0000;memcpy(p+8+offsetof(ACPI_RESOURCE_FIXED_MEMORY32,Address),&value,4);break;
    case 37:value=0x10001;memcpy(p+8+offsetof(ACPI_RESOURCE_FIXED_MEMORY32,AddressLength),&value,4);break;
    case 38:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,InterruptCount)]=2;break;
    case 39:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,Triggering)]=ACPI_EDGE_SENSITIVE;break;
    case 40:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,ProducerConsumer)]=ACPI_PRODUCER;break;
    case 41:out->Length++;break;
    case 42:out->Length-=end;break;
    case 43:out->Length=SIZE_MAX;break;
    case 47:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,Polarity)]=ACPI_ACTIVE_LOW;break;
    case 48:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,Shareable)]=ACPI_SHARED;break;
    case 49:if(i<3)p[len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,WakeCapable)]=1;break;
    case 50:if(i<3){value=0;memcpy(p+len+8+offsetof(ACPI_RESOURCE_EXTENDED_IRQ,Interrupts),&value,4);}break;
    case 51:if(i<3){value=ACPI_RESOURCE_TYPE_FIXED_MEMORY32;memcpy(p+len,&value,4);}break;
    case 52:value=8;memcpy(p+4,&value,4);break;
    case 53:value=ACPI_RESOURCE_TYPE_IO;memcpy(p+len+irq,&value,4);break;
    default:break;
    }
    return AE_OK;
}
static int bus_space_map(bus_space_tag_t t,bus_addr_t a,bus_size_t n,int f,bus_space_handle_t *h)
{
    (void)t;maps++;CHECK(cold==1);CHECK(n==0x10000 && f==BUS_SPACE_MAP_NONPOSTED);
    CHECK(nodes[0].ad_device==&selfdev);CHECK(cixscmi_lease==device_private(&selfdev));
    for(unsigned int i=1;i<5;i++)CHECK(nodes[i].ad_device==&selfdev);
    CHECK(a==(maps==1?0x06590000u:0x065a0000u));
    if(maps==map_fail){if(foreign_on_failure)nodes[2].ad_device=&otherdev;return ENOMEM;}
    *h=a==0x06590000?tx:rx;return 0;
}
static void bus_space_unmap(bus_space_tag_t t,bus_space_handle_t h,bus_size_t n)
{ (void)t;CHECK((h==tx || h==rx) && n==0x10000);CHECK(!sent);unmaps++; }
static uint32_t bus_space_read_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t o)
{
    (void)t;reads++;if(sent && response_mode!=0 && response_mode!=2 && response_mode!=9)CHECK(o!=0xc8);CHECK(o%4==0 && o<0x10000);
    if(h==rx){CHECK(o==0xc0);return rx[o/4];}
    CHECK(h==tx);CHECK(o==0xbc || o==0xc8 || o<=0x7c);
    if(sent && o==4 && response_mode!=1){
        tx[4/4]=1;tx[20/4]=response_mode==2?8:12;tx[28/4]=response_mode==2?response_firmware_status:0;
        tx[32/4]=response_version;if(response_mode==9)tx[20/4]=16;if(response_mode==3)tx[20/4]=8;
        if(response_mode==4)tx[24/4]^=1;
        if(response_mode==7)tx[4/4]=3;
        if(response_mode==8)tx[20/4]=108;
    }
    if(sent && o>=20 && o<=32)CHECK(acquired);
    return tx[o/4];
}
static void bus_space_write_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t o,uint32_t v)
{
    (void)t;writes++;CHECK(h==tx);CHECK(o==0 || o==4 || o==16 || o==20 || o==24 || o==128);
    if(o==4){CHECK(v==0 && published);}
    if(o==128){doorbells++;CHECK(v==1 && ordered);CHECK(tx[0]==0 && tx[4]==0);
        CHECK(tx[5]==4 && tx[6]==expected_header);sent=true;}
    tx[o/4]=v;
}
static void bus_space_barrier(bus_space_tag_t t,bus_space_handle_t h,bus_size_t o,bus_size_t n,int f)
{ (void)t;CHECK(h==tx && o==0 && n>=0x84);CHECK(f==(BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE));barriers++; }
static void test_dsb(void)
{ CHECK(barriers>completions);completions++;if(completions==1)published=true;
    else if(completions==2)ordered=true;else acquired=true; }
static void setup(void)
{
    for(unsigned int i=0;i<5;i++)free(info[i].p);
    memset(&root,0,sizeof(root));memset(nodes,0,sizeof(nodes));memset(info,0,sizeof(info));
    memset(&selfdev,0,sizeof(selfdev));memset(&otherdev,0,sizeof(otherdev));
    rootdev.private=&root;root.sc_head.first=nodes;
    for(unsigned int i=0;i<5;i++){
        info[i].p=calloc(1,sizeof(ACPI_DEVICE_INFO)+40);assert(info[i].p);
        info[i].hid=(char *)(info[i].p+1);info[i].uid=info[i].hid+16;
        nodes[i].ad_handle=&nodes[i];nodes[i].ad_type=ACPI_TYPE_DEVICE;nodes[i].ad_root=&rootdev;
        nodes[i].ad_devinfo=info[i].p;nodes[i].next=i==4?NULL:&nodes[i+1];
        info[i].p->InfoSize=sizeof(ACPI_DEVICE_INFO)+40;info[i].p->Type=ACPI_TYPE_DEVICE;
        info[i].p->Valid=ACPI_VALID_HID|ACPI_VALID_UID;
        strcpy(info[i].hid,i==0?"CIXHA006":i<3?"CIXHA001":i==3?"CIXHA004":"CIXHA005");
        strcpy(info[i].uid,i==1?"6":i==2?"7":"0");
        info[i].p->HardwareId.String=info[i].hid;info[i].p->HardwareId.Length=9;
        info[i].p->UniqueId.String=info[i].uid;info[i].p->UniqueId.Length=2;
    }
    memset(tx,0,sizeof(tx));memset(rx,0,sizeof(rx));tx[1]=1;
    evaluations=maps=unmaps=reads=writes=doorbells=barriers=completions=pauses=0;
    malformed=failed_node=map_fail=response_mode=0;intr=foreign_on_failure=false;
    sent=published=ordered=acquired=false;ticks=0;cold=1;defers=0;deferred=NULL;
    diagnostic[0]=0;cixscmi_lease=NULL;
    response_version=0x20000;response_firmware_status=0xfffffffc;expected_header=0x44000;
}
static void attach(struct cixscmi_acpi_softc *sc,unsigned int flags)
{
    struct acpi_attach_args aa={.aa_node=&nodes[0],.aa_memt=&root};
    memset(sc,0,sizeof(*sc));selfdev.private=sc;selfdev.cf.cf_flags=flags;
    expected_header=flags==2?0x45000:0x44000;
    cixscmi_acpi_attach(&rootdev,&selfdev,&aa);nodes[0].ad_device=&selfdev;
}
static void finish(struct cixscmi_acpi_softc *sc)
{ CHECK(allocations==0);CHECK(pthread_mutex_destroy(&sc->sc_lock.lock)==0); }
static void test_clock_selector(void)
{
    struct cixscmi_acpi_softc sc;
    const uint32_t versions[]={0x10000,0x10001,0,1,0x20000,UINT32_MAX};
    const uint32_t statuses[]={0xffffffff,0xfffffffd,0xfffffffc,0xfffffffa,0xfffffff8,0x80000000};
    for(unsigned int i=0;i<sizeof(versions)/sizeof(versions[0]);i++){
        setup();attach(&sc,2);CHECK(defers==1 && evaluations==0 && maps==0 && writes==0);
        if(defers!=1){finish(&sc);continue;}
        response_version=versions[i];sc.sc_version=0xfeedface;deferred(&selfdev);
        CHECK(sc.sc_error==0 && sc.sc_version_valid && sc.sc_version==versions[i]);
        CHECK(maps==2 && unmaps==0 && doorbells==1 && cixscmi_lease==&sc);
        CHECK(sc.sc_trace.request_header==0x45000 && sc.sc_trace.response_length==12);
        CHECK(strstr(diagnostic,"CLOCK_VERSION")!=NULL);
        CHECK(sc.sc_decode_error==0 && sc.sc_reviewed_version==(i==0));
        CHECK(sc.sc_version_policy_error==(i==0?0:ENOTSUP));
        char expected[128];
        (void)snprintf(expected,sizeof(expected),"helper_return=0 decode_error=0 reviewed_version=%u version_policy_error=%d",
            i==0,i==0?0:ENOTSUP);
        CHECK(strstr(diagnostic,expected)!=NULL);
        unsigned int old_reads=reads,old_writes=writes;deferred(&selfdev);
        CHECK(reads==old_reads && writes==old_writes && doorbells==1);finish(&sc);
    }
    for(unsigned int mode=1;mode<=9;mode++){
        setup();attach(&sc,2);if(defers!=1){finish(&sc);continue;}
        response_mode=mode;sc.sc_version=0xfeedface;
        if(mode==5)tx[1]=3;
        if(mode==6)tx[1]=0;
        deferred(&selfdev);
        CHECK(!sc.sc_version_valid && sc.sc_version==0xfeedface);
        CHECK(maps==2 && unmaps==0 && cixscmi_lease==&sc && sc.sc_query_started);
        CHECK(doorbells==((mode==5 || mode==6)?0u:1u));
        CHECK(sc.sc_ack_after_valid==(mode==2 || mode==9));
        CHECK(sc.sc_decode_error==(mode==9?EPROTO:0));
        CHECK(!sc.sc_reviewed_version && sc.sc_version_policy_error==0);
        CHECK(sc.sc_error==(mode==1?ETIMEDOUT:mode==2?EIO:mode==6?EBUSY:mode==9?0:EPROTO));
        CHECK(sc.sc_trace.firmware_status_valid==(mode==2 || mode==3 || mode==9));
        if(mode==2)CHECK(sc.sc_error==EIO && sc.sc_trace.stage==SCMI_SMT_DIAG_FIRMWARE &&
            sc.sc_trace.firmware_status_valid && sc.sc_trace.firmware_status==-4);
        if(mode==9){
            CHECK(sc.sc_error==0 && sc.sc_trace.stage==SCMI_SMT_DIAG_COMPLETE);
            char expected[64];(void)snprintf(expected,sizeof(expected),"helper_return=0 decode_error=%d",EPROTO);
            CHECK(strstr(diagnostic,expected)!=NULL);
        }
        unsigned int old_reads=reads,old_writes=writes;deferred(&selfdev);
        CHECK(reads==old_reads && writes==old_writes);finish(&sc);
    }
    for(unsigned int i=0;i<sizeof(statuses)/sizeof(statuses[0]);i++){
        setup();attach(&sc,2);if(defers!=1){finish(&sc);continue;}
        response_mode=2;response_firmware_status=statuses[i];deferred(&selfdev);
        CHECK(sc.sc_error==EIO && !sc.sc_version_valid && sc.sc_ack_after_valid);
        CHECK(sc.sc_trace.firmware_status_valid && (uint32_t)sc.sc_trace.firmware_status==statuses[i]);
        CHECK(strstr(diagnostic,"decode_error=0 reviewed_version=0 version_policy_error=0")!=NULL);
        CHECK(doorbells==1);finish(&sc);
    }
    const unsigned int invalid[]={3,4,5,UINT_MAX};
    for(unsigned int i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        setup();attach(&sc,invalid[i]);
        CHECK(defers==0 && evaluations==0 && maps==0 && reads==0 && writes==0);finish(&sc);
    }
}
static void test_uid_capacity(void)
{
    struct cixscmi_acpi_softc sc;
    for(unsigned int n=0;n<5;n++)for(unsigned int mode=0;mode<14;mode++){
        setup();attach(&sc,1);
        ACPI_DEVICE_INFO *p=info[n].p;
        if(mode!=0)p->UniqueId.Length=ACPI_MAX64_DECIMAL_DIGITS+1;
        if(mode==2)memset(info[n].uid+2,0x6d,ACPI_MAX64_DECIMAL_DIGITS-1);
        if(mode==3)p->UniqueId.Length=1;
        if(mode==4)p->UniqueId.Length=0;
        if(mode==5)p->UniqueId.Length=UINT32_MAX;
        if(mode==6)p->UniqueId.Length=25; /* Only24 bytes remain in InfoSize. */
        if(mode==7)p->UniqueId.String=(char *)p+p->InfoSize-1;
        if(mode==8)info[n].uid[1]='x'; /* Expected NUL is mandatory. */
        if(mode==9)info[n].uid[0]='9';
        if(mode==10){p->UniqueId.Length=2;info[n].uid[1]='0';}
        if(mode==11)p->InfoSize=(UINT32)((char *)info[n].uid-(char *)p+20);
        if(mode==12)p->HardwareId.Length=10; /* HID length stays exact. */
        if(mode==13)p->UniqueId.String=NULL;
        deferred(&selfdev);
        if(mode<=2){
            CHECK(sc.sc_error==0 && sc.sc_version_valid && sc.sc_version==0x20000);
            CHECK(maps==2 && doorbells==1 && cixscmi_lease==&sc);
        }else{
            CHECK(sc.sc_error==ENOTSUP && !sc.sc_query_started);
            CHECK(maps==0 && reads==0 && writes==0 && cixscmi_lease==NULL);
            for(unsigned int i=1;i<5;i++)CHECK(nodes[i].ad_device==NULL);
            CHECK(strstr(diagnostic,"stage=identity")!=NULL);
        }
        finish(&sc);
    }
    setup();attach(&sc,1);
    for(unsigned int n=0;n<5;n++){
        info[n].p->UniqueId.Length=ACPI_MAX64_DECIMAL_DIGITS+1;
        memset(info[n].uid+2,0x6d,ACPI_MAX64_DECIMAL_DIGITS-1);
    }
    deferred(&selfdev);
    CHECK(sc.sc_error==0 && sc.sc_version_valid && maps==2 && doorbells==1);
    CHECK(sc.sc_ack_after_valid && cixscmi_lease==&sc);finish(&sc);
}
static void check_diagnostic(const char *stage,unsigned int role,int error)
{
    static const char * const roles[]={"parent","MBX6","MBX7","SHM0","SHM1"};
    char expected[128];
    (void)snprintf(expected,sizeof(expected),"stage=%s role=%s error=%d",stage,roles[role],error);
    CHECK(strstr(diagnostic,expected)!=NULL);
}
static void test_diagnostic_stages(void)
{
    struct cixscmi_acpi_softc sc;
    for(unsigned int n=0;n<5;n++){
        setup();attach(&sc,1);info[n].uid[0]='9';deferred(&selfdev);
        check_diagnostic("identity",n,ENOTSUP);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
        setup();attach(&sc,1);malformed=2;failed_node=n;deferred(&selfdev);
        check_diagnostic("sta",n,ENOTSUP);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
        setup();attach(&sc,1);malformed=4;failed_node=n;deferred(&selfdev);
        check_diagnostic("properties",n,EIO);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
        if(n>=3){
            setup();attach(&sc,1);malformed=3;failed_node=n;deferred(&selfdev);
            check_diagnostic("cca",n,ENOTSUP);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
        }
        if(n>0){
            setup();attach(&sc,1);malformed=30;failed_node=n;deferred(&selfdev);
            check_diagnostic("resources",n,EIO);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
            setup();attach(&sc,1);nodes[n].ad_device=&otherdev;deferred(&selfdev);
            check_diagnostic("ownership",n,EBUSY);CHECK(maps==0 && reads==0 && writes==0);
            CHECK(nodes[n].ad_device==&otherdev);finish(&sc);
        }
    }
    setup();attach(&sc,1);malformed=24;deferred(&selfdev);
    check_diagnostic("references",0,ENOTSUP);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
    setup();attach(&sc,1);cold=0;deferred(&selfdev);
    check_diagnostic("ownership",0,EBUSY);CHECK(maps==0 && reads==0 && writes==0);finish(&sc);
    for(unsigned int n=1;n<=2;n++){
        setup();attach(&sc,1);map_fail=n;deferred(&selfdev);
        check_diagnostic("map",n,ENOMEM);CHECK(reads==0 && writes==0);finish(&sc);
    }
    setup();attach(&sc,1);tx[0xbc/4]=2;deferred(&selfdev);
    check_diagnostic("interrupt-ownership",0,EBUSY);CHECK(writes==0);finish(&sc);
}
static void run_negative(unsigned int m,unsigned int n)
{
    for(unsigned int selector=1;selector<=2;selector++){
    struct cixscmi_acpi_softc sc;setup();malformed=m;failed_node=n;attach(&sc,selector);
    CHECK(defers==1 && evaluations==0 && maps==0 && writes==0);
    if(defers!=1){finish(&sc);continue;}
    deferred(&selfdev);
    CHECK(sc.sc_error!=0 && maps==0 && reads==0 && writes==0 && !sc.sc_query_started);
    for(unsigned int i=1;i<5;i++)CHECK(nodes[i].ad_device==NULL);
    deferred(&selfdev);CHECK(maps==0 && writes==0);finish(&sc);
    }
}
int main(void)
{
    struct cixscmi_acpi_softc sc;alarm(30);(void)test_attach_refs;
    test_clock_selector();test_uid_capacity();test_diagnostic_stages();
    setup();attach(&sc,3);CHECK(defers==0 && evaluations==0 && maps==0 && reads==0 && writes==0);finish(&sc);
    setup();attach(&sc,0);CHECK(defers==0 && evaluations==0 && maps==0 && reads==0 && writes==0);finish(&sc);
    for(unsigned int n=0;n<5;n++){
        for(unsigned int m=4;m<=17;m++)run_negative(m,n);
        run_negative(22,n);run_negative(23,n);run_negative(25,n);run_negative(26,n);
        run_negative(1,n);run_negative(2,n);
        for(unsigned int m=27;m<=29;m++)run_negative(m,n);
        for(unsigned int m=44;m<=46;m++)run_negative(m,n);
        if(n>=3)run_negative(3,n);
        if(n>0){
            for(unsigned int m=30;m<=43;m++)if(n<3 || (m!=38 && m!=39 && m!=40))run_negative(m,n);
            for(unsigned int m=47;m<=53;m++)if(n<3 || m>=52)run_negative(m,n);
        }
    }
    for(unsigned int m=18;m<=21;m++)run_negative(m,0);
    run_negative(24,0);run_negative(18,1);run_negative(18,2);
    for(unsigned int selector=1;selector<=2;selector++)
    for(unsigned int mode=0;mode<18;mode++){
        setup();attach(&sc,selector);
        if(mode==0)cold=0;
        if(mode==1)intr=true;
        if(mode==2)nodes[0].ad_device=&otherdev;
        if(mode==3)nodes[2].ad_device=&otherdev;
        if(mode==4)cixscmi_lease=(void *)&otherdev;
        if(mode==5)info[1].p->HardwareId.String=(void *)&otherdev;
        if(mode==6)info[1].p->InfoSize=1;
        if(mode==7)info[1].p->Valid=0;
        if(mode==8)strcpy(info[1].uid,"7");
        if(mode==9)nodes[1].ad_root=&otherdev;
        if(mode==10)nodes[1].ad_type=ACPI_TYPE_INTEGER;
        if(mode==11)info[1].p->HardwareId.Length=UINT32_MAX;
        if(mode==12)info[0].p->Type=ACPI_TYPE_INTEGER;
        if(mode==13)info[0].p->UniqueId.String=(void *)&otherdev;
        if(mode==14)info[3].hid[8]='x';
        if(mode==15)nodes[4].ad_device=&selfdev;
        if(mode==16)nodes[1].ad_device=&otherdev;
        if(mode==17)nodes[4].ad_device=&otherdev;
        deferred(&selfdev);CHECK(sc.sc_error!=0 && maps==0 && writes==0);
        for(unsigned int i=1;i<5;i++){
            device_t owner=NULL;
            if((mode==3 && i==2) || (mode==16 && i==1) || (mode==17 && i==4))owner=&otherdev;
            if(mode==15 && i==4)owner=&selfdev;
            CHECK(nodes[i].ad_device==owner);
        }
        if(mode==1){intr=false;deferred(&selfdev);CHECK(maps==0 && writes==0);}
        finish(&sc);
    }
    for(unsigned int selector=1;selector<=2;selector++)
    for(unsigned int mode=1;mode<=4;mode++){
        setup();attach(&sc,selector);map_fail=mode<=2?mode:2;foreign_on_failure=mode==3;
        if(mode==4){map_fail=0;tx[0xbc/4]=2;}
        deferred(&selfdev);CHECK(sc.sc_error!=0 && writes==0 && !sc.sc_query_started);
        CHECK(unmaps==(mode==1?0:mode==4?2:1));CHECK(cixscmi_lease==NULL);
        for(unsigned int i=1;i<5;i++)CHECK(nodes[i].ad_device==((mode==3 && i==2)?&otherdev:NULL));
        finish(&sc);
    }
    setup();attach(&sc,1);rx[0xc0/4]=1;deferred(&selfdev);
    CHECK(sc.sc_error!=0 && writes==0 && unmaps==2 && cixscmi_lease==NULL);finish(&sc);
    for(unsigned int mode=0;mode<=8;mode++){
        setup();attach(&sc,1);response_mode=mode;sc.sc_version=0xfeedface;
        if(mode==5)tx[1]=3;
        if(mode==6)tx[1]=0;
        deferred(&selfdev);
        CHECK(maps==2 && unmaps==0 && doorbells==((mode==5 || mode==6)?0u:1u) && sc.sc_query_started);
        CHECK(cixscmi_lease==&sc);for(unsigned int i=1;i<5;i++)CHECK(nodes[i].ad_device==&selfdev);
        CHECK(sc.sc_version_valid==(mode==0));CHECK((sc.sc_error==0)==(mode==0));
        if(mode==0)CHECK(sc.sc_version==0x20000);else CHECK(sc.sc_version==0xfeedface);
        if(mode==2)CHECK(sc.sc_trace.firmware_status_valid && sc.sc_trace.firmware_status==-4);
        CHECK(sc.sc_ack_after_valid==(mode==0 || mode==2));
        unsigned int old_writes=writes,old_reads=reads;deferred(&selfdev);
        CHECK(writes==old_writes && reads==old_reads && doorbells==((mode==5 || mode==6)?0u:1u));finish(&sc);
    }
    printf("CIX SCMI binding contracts: %u checks, %u failures\n",checks,failures);
    for(unsigned int i=0;i<5;i++)free(info[i].p);
    return failures?1:0;
}
#endif
