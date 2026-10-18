/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual attachment and divisor functions; native services only are modeled. */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>
#include <dev/ic/scmi_smt_diag.h>
#include "cixscmi-api.inc"
static unsigned int checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"FAIL line %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
#define __KERNEL_RCSID(a,b)
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
typedef struct device *device_t;
struct cfdata {unsigned int cf_flags;};
typedef struct cfdata *cfdata_t;
struct device {void *private;struct cfdata cf;};
struct acpi_devnode {device_t ad_device,ad_root;ACPI_HANDLE ad_handle;ACPI_DEVICE_INFO *ad_devinfo;uint32_t ad_type;struct acpi_devnode *next;};
struct acpi_softc {struct {struct acpi_devnode *first;}sc_head;};
#define SIMPLEQ_FOREACH(v,h,f) for((v)=(h)->first;(v)!=NULL;(v)=(v)->next)
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
struct acpi_attach_args {struct acpi_devnode *aa_node;bus_space_tag_t aa_memt;};
struct acpi_mem {bus_addr_t ar_base,ar_xbase;bus_size_t ar_length;};
struct acpi_irq {uint32_t ar_index,ar_irq,ar_type;struct {struct acpi_irq *sqe_next;} ar_list;};
struct acpi_resources {int ar_nio,ar_niorange,ar_nmem,ar_nmemrange,ar_nirq;struct {struct acpi_irq *first,**last;} ar_irq;};
struct plcom_instance {unsigned int pi_type;uint32_t pi_flags;bus_space_tag_t pi_iot;bus_space_handle_t pi_ioh;bus_addr_t pi_iobase;};
struct plcom_softc {device_t sc_dev;int sc_frequency,sc_hwflags,sc_swflags,sc_fifolen;struct plcom_instance sc_pi;};
struct device_compatible_entry {const char *compat;int value;};
#define DEVICE_COMPAT_EOL {NULL,0}
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) static void *const attach_refs[]={(void *)(m),(void *)(a)}
#define PLCOM_TYPE_PL011 1
#define PLCOM_TYPE_GENERIC_UART 2
#define PLC_FLAG_32BIT_ACCESS 2
#define IPL_SERIAL 5
#define IPL_VM 6
#define IST_LEVEL 3
static int acpi_resource_parse_ops_default,cold;
static struct device uartdev,rootdev,otherdev;
static struct acpi_softc test_acpi;
static struct acpi_devnode uart,scmi,duplicate;
static union {max_align_t align;uint8_t bytes[8192];} private_storage;
static struct {ACPI_DEVICE_INFO *p;char *strings;} uinfo,sinfo;
static struct acpi_attach_args args;
static struct acpi_mem memory;
static struct acpi_irq interrupt;
static bus_addr_t wanted_base;
static bus_size_t wanted_span;
static struct acpi_resources resources;
static unsigned int maps,commons,irqs,cleanups,evaluations,cache_calls,defers,frees;
static unsigned int malformed;
static bool intr,softintr,ready,rp1,map_fail,irq_fail,resource_fail;
static int cache_error,variant;
static uint64_t cached_rate;
static uint32_t cached_id;
static void (*callback)(device_t);
static char events[128],diagnostic[1024];
static unsigned int nevents;
static void event(char c){if(nevents+1>=sizeof(events)){CHECK(false);return;}events[nevents++]=c;events[nevents]=0;}
static void *device_private(device_t d){return d->private;}
static cfdata_t device_cfdata(device_t d){return &d->cf;}
static int cpu_intr_p(void){return intr;}
static int cpu_softintr_p(void){return softintr;}
static void config_interrupts(device_t d,void(*f)(device_t)){CHECK(d==&uartdev && callback==NULL);callback=f;defers++;event('f');}
static void print(const char *fmt,va_list ap){size_t n=strlen(diagnostic);vsnprintf(diagnostic+n,sizeof(diagnostic)-n,fmt,ap);}
static void aprint_error(const char *fmt,...){va_list ap;va_start(ap,fmt);print(fmt,ap);va_end(ap);}
static void aprint_error_dev(device_t d,const char *fmt,...){(void)d;va_list ap;va_start(ap,fmt);print(fmt,ap);va_end(ap);}
static void aprint_normal_dev(device_t d,const char *fmt,...){(void)d;va_list ap;va_start(ap,fmt);print(fmt,ap);va_end(ap);}
static void aprint_verbose_dev(device_t d,const char *fmt,...){(void)d;va_list ap;va_start(ap,fmt);print(fmt,ap);va_end(ap);}
/* Import both native dispatch and storage: ar_type is ACPICA Triggering. */
void *AcpiOsAllocate(ACPI_SIZE n){return malloc(n);}
#define SIMPLEQ_INSERT_TAIL(h,e,f) do {(e)->f.sqe_next=NULL;*(h)->last=(e);(h)->last=&(e)->f.sqe_next;}while(0)
struct native_walk_arg {bool include_producer;device_t dev;void *context;};
struct native_walk_ops {void (*irq)(device_t,void *,uint32_t,uint32_t);};
static void acpi_res_parse_irq(device_t,void *,uint32_t,uint32_t);
static const struct native_walk_ops native_ops={acpi_res_parse_irq};
#include "resource-parser.inc"
static void decode_irq(uint32_t number,uint8_t triggering)
{
    ACPI_RESOURCE r;
    memset(&r,0,sizeof(r));r.Type=ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
    r.Data.ExtendedIrq.ProducerConsumer=ACPI_CONSUMER;
    r.Data.ExtendedIrq.InterruptCount=1;r.Data.ExtendedIrq.Interrupts[0]=number;
    r.Data.ExtendedIrq.Triggering=triggering;
    resources.ar_nirq=0;resources.ar_irq.first=NULL;resources.ar_irq.last=&resources.ar_irq.first;
    struct native_walk_arg arg={false,&uartdev,&resources};
    native_extended_irq(&arg,&r);
    CHECK(resources.ar_nirq==1&&resources.ar_irq.first!=NULL);
    if(resources.ar_irq.first){interrupt=*resources.ar_irq.first;free(resources.ar_irq.first);}
    resources.ar_irq.first=NULL;resources.ar_irq.last=&resources.ar_irq.first;
    CHECK(interrupt.ar_irq==number&&interrupt.ar_type==triggering);
}
static const struct device_compatible_entry *acpi_compatible_lookup(const struct acpi_attach_args *aa,const struct device_compatible_entry *e){CHECK(aa->aa_node==&uart);return &e[variant];}
static int acpi_compatible_match(const struct acpi_attach_args *aa,const struct device_compatible_entry *e){(void)e;return aa->aa_node->ad_type==ACPI_TYPE_DEVICE;}
static ACPI_STATUS acpi_resource_parse(device_t d,ACPI_HANDLE h,const char *name,struct acpi_resources *r,const void *ops){CHECK(d==&uartdev&&h==&uart&&!strcmp(name,"_CRS")&&ops!=NULL);event('p');wanted_base=memory.ar_base;wanted_span=memory.ar_length;*r=resources;return resource_fail?AE_ERROR:AE_OK;}
static struct acpi_mem *acpi_res_mem(struct acpi_resources *r,int n){return r->ar_nmem>n?&memory:NULL;}
static struct acpi_irq *acpi_res_irq(struct acpi_resources *r,int n){return r->ar_nirq>n?&interrupt:NULL;}
static void acpi_resource_cleanup(struct acpi_resources *r){(void)r;cleanups++;event('x');if(callback){memory.ar_base=UINT64_MAX;memory.ar_length=0;}}
static int bus_space_map(bus_space_tag_t t,bus_addr_t address,bus_size_t size,int flags,bus_space_handle_t *out){CHECK(t==&test_acpi&&address==wanted_base&&size==wanted_span&&flags==0);maps++;event('m');if(map_fail)return EIO;*out=1;return 0;}
static void plcom_attach_subr(struct plcom_softc *s){CHECK(s==(void *)private_storage.bytes);if(defers)CHECK(s->sc_frequency==(int)cached_rate&&cache_calls==1);commons++;event('c');}
static ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE h,const char *name,ACPI_INTEGER *value){CHECK(h==&uart&&!strcmp(name,"raspberrypi,shared-rp1-irq"));event('d');if(!rp1)return AE_NOT_FOUND;*value=1;return AE_OK;}
static int plcomintr(void *p){(void)p;return 0;}
static const char *device_xname(device_t d){CHECK(d==&uartdev);return "plcom0";}
static void *acpi_intr_establish(device_t d,uint64_t h,int ipl,bool mpsafe,int(*fn)(void *),void *arg,const char *name){CHECK(d==&uartdev&&h==(uint64_t)(uintptr_t)&uart&&ipl==(rp1?IPL_VM:IPL_SERIAL)&&mpsafe&&fn==plcomintr&&arg==(void *)private_storage.bytes&&name!=NULL);irqs++;event('i');return irq_fail?NULL:&interrupt;}
int cixscmi_acpi_uart_rate(ACPI_HANDLE h,uint32_t id,uint64_t *out){CHECK(h==&scmi&&(id==264||id==266));cache_calls++;cached_id=id;event('r');if(!ready)return ENXIO;if(cache_error)return cache_error;*out=cached_rate;return 0;}
ACPI_STATUS AcpiGetHandle(ACPI_HANDLE parent,const char *name,ACPI_HANDLE *out){CHECK(parent==&uart&&!strcmp(name,"CLKT"));if(malformed==20)return AE_NOT_FOUND;*out=&malformed;return AE_OK;}
ACPI_STATUS AcpiGetType(ACPI_HANDLE h,ACPI_OBJECT_TYPE *out){CHECK(h==&malformed);if(malformed==23)return AE_ERROR;*out=malformed==21?ACPI_TYPE_METHOD:ACPI_TYPE_PACKAGE;return AE_OK;}
ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE h,const char *name,ACPI_OBJECT_LIST *in,ACPI_BUFFER *out){
    struct table {ACPI_OBJECT o[9];char names[32];} *t=calloc(1,sizeof(*t));assert(t);
    CHECK(h==&uart&&!strcmp(name,"CLKT")&&in==NULL);evaluations++;event('t');out->Pointer=t;out->Length=sizeof(*t);
    t->o[0].Type=ACPI_TYPE_PACKAGE;t->o[0].Package.Count=2;t->o[0].Package.Elements=&t->o[1];
    for(unsigned int i=0;i<2;i++){
        t->o[1+i].Type=ACPI_TYPE_PACKAGE;t->o[1+i].Package.Count=3;t->o[1+i].Package.Elements=&t->o[3+3*i];
        ACPI_OBJECT *p=&t->o[3+3*i];p[0].Type=ACPI_TYPE_INTEGER;p[0].Integer.Value=(uinfo.strings[16]=='4'?249:247)+(i==1?17:0);
        strcpy(t->names+i*16,i==0?"apb_pclk":"uartclk");p[1].Type=ACPI_TYPE_STRING;p[1].String.Pointer=t->names+i*16;p[1].String.Length=(UINT32)strlen(p[1].String.Pointer);
        p[2].Type=ACPI_TYPE_LOCAL_REFERENCE;p[2].Reference.ActualType=ACPI_TYPE_DEVICE;p[2].Reference.Handle=&uart;
    }
    if(malformed==1)t->o[0].Type=ACPI_TYPE_INTEGER;
    if(malformed==2)t->o[0].Package.Count=3;
    if(malformed==3)t->o[0].Package.Elements=(void *)&otherdev;
    if(malformed==4)t->o[2].Package.Count=UINT32_MAX;
    if(malformed==5)t->o[6].Integer.Value=265;
    if(malformed==6)strcpy(t->names+16,"apb_pclk");
    if(malformed==7)t->o[8].Reference.Handle=&scmi;
    if(malformed==8)t->o[7].String.Pointer=NULL;
    if(malformed==9)t->o[7].String.Length=UINT32_MAX;
    if(malformed==10)t->o[8].Reference.ActualType=ACPI_TYPE_METHOD;
    if(malformed==11)out->Length=sizeof(ACPI_OBJECT)-1;
    if(malformed==12)t->names[23]='x';
    if(malformed==13)t->o[3].Integer.Value=264;
    return malformed==22?AE_ERROR:AE_OK;
}
void AcpiOsFree(void *p){if(p){frees++;free(p);}}
#include "plcom-source.inc"
#include "divisor-source.inc"
static void identity(ACPI_DEVICE_INFO *p,char *str,const char *hid,const char *uid){memset(p,0,sizeof(*p)+64);p->InfoSize=sizeof(*p)+64;p->Type=ACPI_TYPE_DEVICE;p->Valid=ACPI_VALID_HID|ACPI_VALID_UID;strcpy(str,hid);strcpy(str+16,uid);p->HardwareId.String=str;p->HardwareId.Length=(UINT32)strlen(hid)+1;p->UniqueId.String=str+16;p->UniqueId.Length=21;memset(str+18,0x6d,19);}
static void setup(unsigned int unit,bool cix){
    free(uinfo.p);free(sinfo.p);uinfo.p=calloc(1,sizeof(ACPI_DEVICE_INFO)+64);sinfo.p=calloc(1,sizeof(ACPI_DEVICE_INFO)+64);assert(uinfo.p&&sinfo.p);uinfo.strings=(char *)(uinfo.p+1);sinfo.strings=(char *)(sinfo.p+1);
    memset(private_storage.bytes,0,sizeof(private_storage.bytes));memset(&uart,0,sizeof(uart));memset(&scmi,0,sizeof(scmi));memset(&duplicate,0,sizeof(duplicate));
    uartdev.private=private_storage.bytes;uartdev.cf.cf_flags=1;rootdev.private=&test_acpi;otherdev.private=&test_acpi;
    identity(uinfo.p,uinfo.strings,"ARMH0011",unit==2?"3":unit==3?"4":"2");identity(sinfo.p,sinfo.strings,cix?"CIXHA006":"OTHER000","0");
    uart.ad_handle=&uart;uart.ad_type=ACPI_TYPE_DEVICE;uart.ad_root=&rootdev;uart.ad_devinfo=uinfo.p;uart.next=&scmi;
    scmi.ad_handle=&scmi;scmi.ad_type=ACPI_TYPE_DEVICE;scmi.ad_root=&rootdev;scmi.ad_devinfo=sinfo.p;test_acpi.sc_head.first=&uart;
    args.aa_node=&uart;args.aa_memt=&test_acpi;memory.ar_base=unit==2?0x040d0000:unit==3?0x040e0000:0x040c0000;memory.ar_xbase=memory.ar_base;memory.ar_length=0x1000;
    memset(&resources,0,sizeof(resources));resources.ar_nmem=1;decode_irq(unit==2?330:unit==3?331:329,0);
    maps=commons=irqs=cleanups=evaluations=cache_calls=defers=frees=nevents=malformed=0;events[0]=diagnostic[0]=0;callback=NULL;intr=softintr=rp1=map_fail=irq_fail=resource_fail=false;ready=false;cold=1;cache_error=0;variant=1;cached_rate=100000000;cached_id=0;
}
static void attach(void){plcom_acpi_attach(&rootdev,&uartdev,&args);uart.ad_device=&uartdev;}
static void run_deferred(void){if(callback){void(*f)(device_t)=callback;cold=0;f(&uartdev);}}
static void tests(void){
    for(unsigned int unit=1;unit<=3;unit+=2){setup(unit,true);CHECK(plcom_acpi_match(&rootdev,&uartdev.cf,&args)!=0);attach();CHECK(maps==0&&commons==0&&irqs==0&&defers==1&&cleanups==1&&evaluations==1);ready=true;run_deferred();
#if NCIXSCMI_ACPI
        CHECK(maps==1&&commons==1&&irqs==1&&cache_calls==1&&cached_id==(unit==1?264U:266U));CHECK(((struct plcom_softc *)private_storage.bytes)->sc_frequency==100000000);
#else
        CHECK(maps==0&&commons==0&&irqs==0&&cache_calls==0);
#endif
        unsigned int n=nevents;run_deferred();CHECK(nevents==n);
    }
    for(unsigned int kind=0;kind<4;kind++){setup(kind==0?2:1,kind<2);if(kind==1)uartdev.cf.cf_flags=0;if(kind==2){variant=0;strcpy(uinfo.strings,"BCM2837");uinfo.p->HardwareId.Length=8;}if(kind==3)rp1=true;attach();CHECK(!callback&&maps==1&&commons==1&&irqs==1&&evaluations==0&&cache_calls==0);CHECK(!strcmp(events,"pmcdix"));CHECK(((struct plcom_softc *)private_storage.bytes)->sc_frequency==0);}
    for(unsigned int m=1;m<=23;m++){if(m>13&&m<20)continue;setup(1,true);malformed=m;attach();CHECK(maps==0&&commons==0&&irqs==0&&defers==0&&cleanups==1);CHECK(frees==evaluations);CHECK(diagnostic[0]!=0);}
    for(unsigned int kind=0;kind<22;kind++){setup(1,true);if(kind==0)resources.ar_nmem=2;if(kind==1)resources.ar_nirq=2;if(kind==2)memory.ar_length=0x1001;if(kind==3)memory.ar_base++;if(kind==4)interrupt.ar_irq++;if(kind==5)decode_irq(329,1);if(kind==6)uinfo.p->UniqueId.Length=UINT32_MAX;if(kind==7)uinfo.p->UniqueId.String=NULL;if(kind==8)sinfo.p->UniqueId.String=NULL;if(kind==9){duplicate=scmi;scmi.next=&duplicate;}if(kind==10)cold=0;if(kind==11)intr=true;if(kind==12)resources.ar_nmemrange=1;if(kind==13)memory.ar_xbase++;if(kind==14)softintr=true;if(kind==15)resources.ar_niorange=1;if(kind==16)uinfo.p->HardwareId.String=NULL;if(kind==17)uinfo.p->HardwareId.Length=UINT32_MAX;if(kind==18)uinfo.strings[17]='x';if(kind==19)uinfo.p->InfoSize=sizeof(ACPI_DEVICE_INFO)-1;if(kind==20)uart.ad_type=ACPI_TYPE_METHOD;if(kind==21)decode_irq(329,3);attach();CHECK(maps==0&&commons==0&&irqs==0&&defers==0&&cleanups==1);}
    for(unsigned int kind=0;kind<11;kind++){setup(1,true);attach();CHECK(callback!=NULL);if(!callback)continue;ready=true;if(kind==0)ready=false;if(kind==1)cache_error=EIO;if(kind==2)cached_rate=0;if(kind==3)cached_rate=(uint64_t)INT_MAX+1;if(kind==4)cached_rate=UINT64_MAX;if(kind==5)uart.ad_device=&otherdev;if(kind==6)uart.ad_root=&otherdev;if(kind==7)test_acpi.sc_head.first=&scmi;if(kind==8)intr=true;if(kind==9)softintr=true;if(kind==10)map_fail=true;run_deferred();CHECK(commons==0&&irqs==0);CHECK(maps==(kind==10&&NCIXSCMI_ACPI?1U:0U));unsigned int n=nevents;run_deferred();CHECK(nevents==n);}
    setup(1,true);attach();ready=true;cached_rate=INT_MAX;run_deferred();CHECK(commons==(NCIXSCMI_ACPI?1U:0U));
    setup(1,true);attach();ready=true;irq_fail=true;run_deferred();CHECK(commons==(NCIXSCMI_ACPI?1U:0U));CHECK(strstr(diagnostic,"interrupt")!=NULL||!NCIXSCMI_ACPI);
}
static void provider_first_test(void){setup(3,true);ready=true;attach();CHECK(defers==1&&maps==0);run_deferred();CHECK(commons==(NCIXSCMI_ACPI?1U:0U));}
static void divisor_tests(void)
{
    const struct {long speed,frequency;int encoded;}cases[]={
        {230400,100000000,27*64+8},{115200,100000000,54*64+16},{9600,100000000,651*64+3},
        {100,3199,128},{115200,0,0},{-1,100000000,-1},{1L<<28,100000000,-1},{0,100000000,-1},
        {115200,-1,-1},{1,LONG_MAX,-1},{LONG_MAX,LONG_MAX,-1},{LONG_MAX/16,LONG_MAX,64},{1,16,64},{1,1,-1},{4,4194239,4194239},{4,4194240,4194240},{4,4194241,-1},{4,4194303,-1},{4,4194304,-1}};
    for(unsigned int i=0;i<__arraycount(cases);i++)CHECK(pl011comspeed(cases[i].speed,cases[i].frequency)==cases[i].encoded);
}
int main(void){CHECK(attach_refs[0]!=NULL&&attach_refs[1]!=NULL);(void)device_cfdata;(void)cpu_intr_p;(void)cpu_softintr_p;(void)config_interrupts;(void)aprint_normal_dev;tests();provider_first_test();divisor_tests();free(uinfo.p);free(sinfo.p);printf("CIX PL011: %u checks, %u failures\n",checks,failures);return failures?1:0;}
