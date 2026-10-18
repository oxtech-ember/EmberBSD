/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual attachment functions; native/ACPICA services are modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "queue.h"
#include "ioccom.h"
#include "gpio.h"
#define ACPI_APPLICATION
#include <acpi.h>

#ifdef CIXGPIOINPUT_API
#define ABSENT_UNUSED
#else
/* These native services are unused only in the missing-implementation RED. */
#define ABSENT_UNUSED __attribute__((unused))
#endif
static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
typedef void *device_t;
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
struct test_cfdata { int cf_flags; const char *cf_name; };
typedef struct test_cfdata *cfdata_t;
struct acpi_devnode { ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL {NULL}
#define CFATTACH_DECL_NEW(n,s,m,a,d,r) \
    static const void *const attachment[] = {(const void *)(m),(const void *)(a)}
static char handles[3];
#define GPIO ((ACPI_HANDLE)&handles[0])
#define MUX ((ACPI_HANDLE)&handles[1])
#define OTHER ((ACPI_HANDLE)&handles[2])
static struct test_cfdata fixture_cf;
static struct acpi_devnode node;
static struct acpi_attach_args fixture_aa;
static ACPI_RESOURCE gpio_res[66], mux_res[66];
static unsigned int gpio_count, mux_count;
static ACPI_INTEGER gpio_uid, mux_uid, bank_id, ngpios, io_mask;
static const char *gpio_hid, *mux_hid;
static bool compatible, missing_gpio, missing_mux, wrong_gpio, wrong_mux;
static unsigned int evals, live_strings, walks, maps, unmaps, reads, forbidden;
static unsigned int map_failure, eval_failure, walk_failure;
static unsigned int stack[2], depth;
typedef unsigned long u_long;
static unsigned int writes, barriers, children, child_failure;
static uint32_t hw_mux[3], hw_bypass, hw_direction, hw_input;
static bool readback_failure;

static _Alignas(max_align_t) unsigned char softc_storage[16384];
typedef int kmutex_t; typedef int kcondvar_t; typedef int pmf_qual_t;
#define MUTEX_DEFAULT 0
#define IPL_VM 5
static unsigned int mutexes;
static kmutex_t *locked;
static ABSENT_UNUSED void mutex_init(kmutex_t *m,int type,int ipl)
{ CHECK(type==0 && ipl==IPL_VM);*m=0;mutexes++; }
static ABSENT_UNUSED void mutex_destroy(kmutex_t *m) { CHECK(*m==0 && mutexes>0);mutexes--; }
static void mutex_enter(kmutex_t *m) { CHECK(*m==0 && locked==NULL);*m=1;locked=m; }
static void mutex_exit(kmutex_t *m) { CHECK(*m==1 && locked==m);*m=0;locked=NULL; }
static void *device_private(device_t d);
#include "gpiovar.h"
struct lwp { void *l_cred; };
struct fixture_cfargs { const int *locators; const char *iattr; };
#define CFARGS(...) (struct fixture_cfargs){__VA_ARGS__}
#define CFARGS_NONE (struct fixture_cfargs){0}
#define GPIOCF_NLOCS 3
#define GPIOCF_OFFSET 0
#define GPIOCF_MASK 1
#define GPIOCF_FLAG 2
#ifndef FWRITE
#define FWRITE 2
#endif
#define KAUTH_DEVICE_GPIO_PINSET 0
#define KERNEL_LOCK(a,b) ((void)(a),(void)(b))
#define KERNEL_UNLOCK_ONE(a) ((void)(a))
#define DPRINTF(...) ((void)0)
#define KM_SLEEP 0
static int cv_wait_sig(kcondvar_t *c,kmutex_t *m) { (void)c;(void)m;CHECK(false);return EINTR; }
static void cv_signal(kcondvar_t *c) { (void)c; }
static bool device_is_active(device_t d) { (void)d;return true; }
static int kauth_authorize_device(void *a,int b,void *c,void *d,void *e,void *f)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0; }
static void kmem_free(void *p,size_t n) { (void)n;free(p); }
static void *kmem_alloc(size_t n,int flags) { CHECK(flags==KM_SLEEP);void *p=malloc(n);if(p==NULL)abort();return p; }

static size_t fixture_strlcpy(char *dst,const char *src,size_t n)
{ size_t len=strlen(src);if(n){size_t copy=len<n-1?len:n-1;memcpy(dst,src,copy);dst[copy]=0;}return len; }
#define strlcpy fixture_strlcpy
static cfdata_t config_search(device_t p,void *a,struct fixture_cfargs args)
{ (void)p;(void)a;(void)args;CHECK(false);return NULL; }
static device_t config_attach(device_t p,cfdata_t cf,void *a,int (*print)(void *,const char *),struct fixture_cfargs args)
{ (void)p;(void)cf;(void)a;(void)print;(void)args;CHECK(false);return NULL; }
int gpiobus_print(void *arg,const char *name) { (void)arg;(void)name;return 0; }

static ABSENT_UNUSED void print_output(const char *,...);
#define UNCONF 2
#define aprint_normal print_output
#include "gpio-consumers.inc"
#undef aprint_normal
static struct gpio_softc framework;
static void *device_private(device_t d) { return d==&framework ? (void *)&framework : softc_storage; }
static ABSENT_UNUSED device_t config_found(device_t p,void *arg,int (*print)(void *,const char *),struct fixture_cfargs args)
{
    (void)p;(void)print;(void)args;struct gpiobus_attach_args *gba=arg;
    CHECK(depth==1 && writes==1 && barriers==1 && (hw_direction&7)==7);children++;
    if(child_failure)return NULL;
    framework.sc_gc=gba->gba_gc;framework.sc_pins=gba->gba_pins;framework.sc_npins=gba->gba_npins;
    CHECK(framework.sc_npins==3);return &framework;
}
static bool eval_injected;

static char output[2048];
static size_t output_used;
static UINT16 pins[2];
static UINT8 vendor[8];
static char fixture_label[8];

void AcpiOsFree(void *p) { CHECK(live_strings!=0); live_strings--; free(p); }
static ABSENT_UNUSED void print_output(const char *format, ...)
{
    va_list ap;
    va_start(ap,format);
    int n=vsnprintf(output+output_used,sizeof(output)-output_used,format,ap);
    va_end(ap);
    CHECK(n>=0 && (size_t)n<sizeof(output)-output_used);
    if(n>=0 && (size_t)n<sizeof(output)-output_used) output_used+=(size_t)n;
}
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_error_dev(d,...) ((void)(d),print_output(__VA_ARGS__))
#define aprint_normal_dev(d,...) ((void)(d),print_output(__VA_ARGS__))
static ABSENT_UNUSED cfdata_t device_cfdata(device_t d) { (void)d;return &fixture_cf; }
static ABSENT_UNUSED int acpi_compatible_match(const struct acpi_attach_args *arg,
    const struct device_compatible_entry *entry)
{
    CHECK(arg==&fixture_aa && strcmp(entry[0].compat,"CIXH1003")==0);
    return compatible ? 1 : 0;
}
ACPI_STATUS AcpiGetHandle(ACPI_HANDLE parent,const char *path,ACPI_HANDLE *out)
{
    CHECK(parent==NULL); evals++;
    if(strcmp(path,"\\_SB.GPI5")==0) {
        if(missing_gpio) return AE_NOT_FOUND;
        *out=wrong_gpio ? OTHER : GPIO;
    } else {
        CHECK(strcmp(path,"\\_SB.MUX1")==0);
        if(missing_mux) return AE_NOT_FOUND;
        *out=wrong_mux ? OTHER : MUX;
    }
    return AE_OK;
}
static ABSENT_UNUSED ACPI_STATUS acpi_eval_integer(ACPI_HANDLE h,const char *name,ACPI_INTEGER *out)
{
    evals++;
    if(eval_failure==evals) { eval_injected=true; return AE_ERROR; }
    if(h!=GPIO && h!=MUX) return AE_BAD_DATA;
    CHECK(strcmp(name,"_UID")==0);
    *out=h==GPIO ? gpio_uid : mux_uid;
    return AE_OK;
}
static ABSENT_UNUSED ACPI_STATUS acpi_eval_string(ACPI_HANDLE h,const char *name,char **out)
{
    evals++;
    if(eval_failure==evals) { eval_injected=true; return AE_ERROR; }
    if(h!=GPIO && h!=MUX) return AE_BAD_DATA;
    CHECK(strcmp(name,"_HID")==0);
    const char *value=h==GPIO ? gpio_hid : mux_hid;
    *out=malloc(strlen(value)+1);
    if(*out==NULL) abort();
    strcpy(*out,value);live_strings++;
    return AE_OK;
}
static ABSENT_UNUSED ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE h,const char *name,ACPI_INTEGER *out)
{
    evals++;
    if(eval_failure==evals) { eval_injected=true; return AE_ERROR; }
    CHECK(h==GPIO);
    if(strcmp(name,"id")==0) *out=bank_id;
    else if(strcmp(name,"ngpios")==0) *out=ngpios;
    else { CHECK(strcmp(name,"gpio-io-mask")==0); *out=io_mask; }
    return AE_OK;
}
ACPI_STATUS AcpiWalkResources(ACPI_HANDLE h,const char *name,
    ACPI_WALK_RESOURCE_CALLBACK fn,void *cookie)
{
    CHECK(strcmp(name,"_CRS")==0 && (h==GPIO || h==MUX));walks++;
    if(walk_failure==walks) return AE_BAD_DATA;
    ACPI_RESOURCE *res=h==GPIO ? gpio_res : mux_res;
    unsigned int count=h==GPIO ? gpio_count : mux_count;
    for(unsigned int i=0;i<count;i++) {
        ACPI_STATUS rv=fn(&res[i],cookie);
        if(ACPI_FAILURE(rv)) return rv;
    }
    return AE_OK;
}
static ABSENT_UNUSED int bus_space_map(bus_space_tag_t tag,bus_addr_t address,bus_size_t size,
    int flags,bus_space_handle_t *out)
{
    CHECK(tag==&handles && flags==0 && size==0x1000);
    CHECK(walks==2 && reads==0 && live_strings==0);
    unsigned int index=address==0x16005000 ? 0 : 1;
    CHECK(address==(index==0 ? 0x16005000 : 0x16007000));
    maps++;
    if(maps==map_failure) return 1;
    CHECK(depth<2); if(depth<2)stack[depth++]=index;
    *out=(bus_space_handle_t)address;
    return 0;
}
static ABSENT_UNUSED void bus_space_unmap(bus_space_tag_t tag,bus_space_handle_t handle,bus_size_t size)
{
    CHECK(tag==&handles && size==0x1000 && depth>0);
    if(depth>0) {
        unsigned int index=stack[--depth];
        CHECK(handle==(index==0 ? 0x16005000 : 0x16007000));
    }
    unmaps++;
}
static ABSENT_UNUSED uint32_t bus_space_read_4(bus_space_tag_t tag,bus_space_handle_t handle,bus_size_t offset)
{
    CHECK(tag==&handles && depth>0);reads++;
    if(handle==0x16007000) {
        CHECK(depth==2 && writes==0);
        if(offset!=0x80 && offset!=0x84 && offset!=0x88) { CHECK(false);return 0; }
        return hw_mux[(offset-0x80)/4];
    }
    CHECK(handle==0x16005000);
    if(offset==0)return hw_bypass;
    if(offset==4)return hw_direction ^ (readback_failure && writes ? 1U : 0U);
    if(offset==0x10)return hw_input;
    CHECK(false);forbidden++;return 0;
}
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
static ABSENT_UNUSED void bus_space_write_4(bus_space_tag_t tag,bus_space_handle_t handle,bus_size_t offset,uint32_t value)
{
    CHECK(tag==&handles && handle==0x16005000 && offset==4 && depth==1);
    CHECK(value==(hw_direction|7) && writes==0 && locked!=NULL && *locked==1);writes++;hw_direction=value;
}
static ABSENT_UNUSED void bus_space_barrier(bus_space_tag_t tag,bus_space_handle_t handle,bus_size_t offset,bus_size_t size,int flags)
{ CHECK(tag==&handles && handle==0x16005000 && offset==4 && size==4 && flags==(BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE) && writes==1);barriers++; }
static __attribute__((unused)) void prohibited(const char *what) { (void)what; forbidden++;CHECK(false); }

#define acpi_intr_establish_irq(...) (prohibited("IRQ"),(void *)NULL)
#define acpi_event_create_gpio(...) (prohibited("event"),AE_ERROR)

#ifdef CIXGPIOINPUT_API
#include "cixgpio-input-source.inc"
#else
static int cixgpioin_acpi_match(device_t p,cfdata_t c,void *a)
{ (void)p;(void)c;(void)a;return 0; }
static void cixgpioin_acpi_attach(device_t p,device_t s,void *a)
{ (void)p;(void)s;(void)a; }
#endif

static void reset(void)
{
    memset(gpio_res,0,sizeof(gpio_res));memset(mux_res,0,sizeof(mux_res));
    evals=live_strings=walks=maps=unmaps=reads=forbidden=depth=0;
    map_failure=eval_failure=walk_failure=0;eval_injected=false;
    writes=barriers=children=child_failure=0;readback_failure=false;mutexes=0;locked=NULL;
    hw_mux[0]=hw_mux[1]=hw_mux[2]=0x57;hw_bypass=0;hw_direction=0xa5a50000;hw_input=5;
    memset(&framework,0,sizeof(framework));memset(softc_storage,0,sizeof(softc_storage));
    compatible=true; missing_gpio=missing_mux=wrong_gpio=wrong_mux=false;
    gpio_uid=5;mux_uid=1;bank_id=1;ngpios=10;io_mask=0;
    gpio_hid="CIXH1003";mux_hid="CIXHA017";
    fixture_cf.cf_flags=1;node.ad_handle=GPIO;fixture_aa.aa_node=&node;fixture_aa.aa_memt=&handles;
    gpio_count=mux_count=3;output_used=0;output[0]='\0';
    for(unsigned int i=0;i<66;i++) {
        gpio_res[i].Length=mux_res[i].Length=sizeof(ACPI_RESOURCE);
        gpio_res[i].Type=mux_res[i].Type=ACPI_RESOURCE_TYPE_END_TAG;
    }
    gpio_res[0].Type=mux_res[0].Type=ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
    gpio_res[0].Data.FixedMemory32=(ACPI_RESOURCE_FIXED_MEMORY32){1,0x16005000,0x1000};
    mux_res[0].Data.FixedMemory32=(ACPI_RESOURCE_FIXED_MEMORY32){1,0x16007000,0x1000};
    gpio_res[1].Type=ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
    ACPI_RESOURCE_EXTENDED_IRQ *irq=&gpio_res[1].Data.ExtendedIrq;
    irq->ProducerConsumer=1;irq->InterruptCount=1;irq->Interrupts[0]=405;
    /* Zero is level/high/exclusive/non-wake in the native decoded schema. */
    mux_res[1].Type=ACPI_RESOURCE_TYPE_PIN_GROUP;
    pins[0]=32;pins[1]=34;memcpy(fixture_label,"group",6);
    const uint8_t init[]={0,0x80,1,0x44,0,0x88,1,0x44};memcpy(vendor,init,sizeof(init));
    ACPI_RESOURCE_PIN_GROUP *g=&mux_res[1].Data.PinGroup;
    g->RevisionId=1;g->PinTableLength=2;g->PinTable=pins;
    g->VendorLength=8;g->VendorData=vendor;
    g->ResourceLabel.StringLength=6;g->ResourceLabel.StringPtr=fixture_label;
}
/* Host-only release of built-in lifetime mapping; not a production detach. */
static void drop(void)
{
    if(depth)bus_space_unmap(&handles,0x16005000,0x1000);
    CHECK(depth==0);mutexes=0;
}
static void run_attach(void)
{ cixgpioin_acpi_attach(NULL,&handles,&fixture_aa);CHECK(live_strings==0 && forbidden==0); }
static void reject(void)
{ run_attach();CHECK(maps==0 && reads==0 && unmaps==0 && writes==0 && children==0 && depth==0); }
static void consumer_tests(void)
{
    struct lwp l={0};
    CHECK(framework.sc_gc->gp_pin_read!=NULL && framework.sc_gc->gp_pin_write!=NULL && framework.sc_gc->gp_pin_ctl!=NULL);
    if(framework.sc_gc->gp_pin_read==NULL || framework.sc_gc->gp_pin_write==NULL || framework.sc_gc->gp_pin_ctl==NULL)return;
    CHECK(framework.sc_gc->gp_intr_establish==NULL && framework.sc_gc->gp_intr_disestablish==NULL && framework.sc_gc->gp_intr_str==NULL);
    for(int pin=0;pin<3;pin++) {
        CHECK(framework.sc_pins[pin].pin_num==pin && framework.sc_pins[pin].pin_caps==GPIO_PIN_INPUT);
        CHECK(framework.sc_pins[pin].pin_flags==GPIO_PIN_INPUT && framework.sc_pins[pin].pin_mapped==1 && framework.sc_pins[pin].pin_intrcaps==0);
        for(unsigned int value=0;value<8;value++) {
            hw_input=value;struct gpio_req req={.gp_pin=pin};
            unsigned int r=reads;
            CHECK(gpio_ioctl(&framework,GPIOREAD,&req,0,&l)==0 && req.gp_value==(int)((value>>pin)&1) && reads==r+1);
        }
        struct gpio_req req={.gp_pin=pin,.gp_value=1};unsigned int w=writes;
        CHECK(gpio_ioctl(&framework,GPIOWRITE,&req,FWRITE,&l)==EBUSY);
        CHECK(gpio_ioctl(&framework,GPIOTOGGLE,&req,FWRITE,&l)==EBUSY);
        struct gpio_set set={.gp_pin=pin,.gp_flags=GPIO_PIN_OUTPUT};
        CHECK(gpio_ioctl(&framework,GPIOSET,&set,FWRITE,&l)==ENODEV);
        set.gp_flags=GPIO_PIN_PULLUP;CHECK(gpio_ioctl(&framework,GPIOSET,&set,FWRITE,&l)==ENODEV);
        set.gp_flags=GPIO_PIN_ALT0;CHECK(gpio_ioctl(&framework,GPIOSET,&set,FWRITE,&l)==ENODEV);
        set.gp_flags=GPIO_PIN_INPUT;CHECK(gpio_ioctl(&framework,GPIOSET,&set,FWRITE,&l)==0);
        CHECK(gpio_ioctl(&framework,GPIOUNSET,&set,FWRITE,&l)==EBUSY);
        struct gpio_attach attach={.ga_offset=pin,.ga_mask=1};
        CHECK(gpio_ioctl(&framework,GPIOATTACH,&attach,FWRITE,&l)==EBUSY);
        CHECK(writes==w);
    }
    CHECK(framework.sc_gc->gp_pin_write!=NULL && framework.sc_gc->gp_pin_ctl!=NULL);
    unsigned int w=writes,r=reads;
    for(int pin=-1;pin<=3;pin+=4) {
        struct gpio_req req={.gp_pin=pin,.gp_value=1};
        CHECK(gpio_ioctl(&framework,GPIOREAD,&req,0,&l)==EINVAL);
        CHECK(gpio_ioctl(&framework,GPIOWRITE,&req,FWRITE,&l)==EINVAL);
    }
    CHECK(writes==w && reads==r);
    CHECK(gpio_resume((void *)&framework,NULL));
    CHECK(writes==w && reads==r); /* Resume cannot write outputs or new configuration. */
}
static void mask_boundary_tests(void)
{
    const uint32_t masks[]={0,1,0x80000000,0x80008000,UINT32_MAX};
    const int counts[]={0,1,1,2,32};
    for(unsigned int i=0;i<__arraycount(masks);i++) {
        gpio_pin_t p[32]={0};struct gpio_softc sc={.sc_pins=p,.sc_npins=32};
        int storage[34];for(unsigned int j=0;j<34;j++)storage[j]=0x76543210;
        struct gpio_pinmap map={.pm_map=storage+1};
        CHECK(gpio_npins(masks[i])==counts[i]);
        CHECK(gpio_pin_can_map(&sc,0,masks[i])==1);
        CHECK(gpio_pin_map(&sc,0,masks[i],&map)==0 && map.pm_size==counts[i]);
        CHECK(storage[0]==0x76543210 && storage[33]==0x76543210);
        int n=0;for(int bit=0;bit<32;bit++)if(masks[i]&(UINT32_C(1)<<bit)) {
            CHECK(map.pm_map[n++]==bit && p[bit].pin_mapped==1);
        }
        struct gpio_attach_args arg={.ga_mask=masks[i]};
        output_used=0;output[0]=0;CHECK(gpio_print(&arg,NULL)==UNCONF);
        if(masks[i]&0x80000000U)CHECK(strstr(output," 31")!=NULL);
    }
    gpio_pin_t p[32]={0};struct gpio_softc sc={.sc_pins=p,.sc_npins=32};
    CHECK(gpio_pin_can_map(&sc,1,0x80000000)==0);
}
int main(void)
{
    mask_boundary_tests();
    reset();
    CHECK(cixgpioin_acpi_match(NULL,&fixture_cf,&fixture_aa)>0);
    run_attach();CHECK(maps==2 && unmaps==1 && depth==1 && writes==1 && barriers==1 && children==1);
    if(framework.sc_gc==NULL) { fprintf(stderr,"FAIL: no native input service\n");return 1; }
    CHECK(hw_direction==0xa5a50007);consumer_tests();drop();
    for(unsigned int i=0;i<4;i++) {
        reset();fixture_cf.cf_flags=(int[]){0,2,3,-1}[i];
        CHECK(cixgpioin_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();CHECK(evals==0 && walks==0);
    }
    reset();node.ad_handle=OTHER;CHECK(cixgpioin_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();
    reset();compatible=false;CHECK(cixgpioin_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();
    for(unsigned int fault=0;fault<15;fault++) {
        reset();
        switch(fault) {
        case 0:missing_gpio=true;break;case 1:missing_mux=true;break;
        case 2:wrong_gpio=true;break;case 3:wrong_mux=true;break;
        case 4:gpio_uid=4;break;case 5:mux_uid=2;break;
        case 6:gpio_hid="CIXH1002";break;case 7:mux_hid="CIXHA016";break;
        case 8:bank_id=2;break;case 9:ngpios=32;break;case 10:io_mask=1;break;
        case 11:walk_failure=1;break;case 12:walk_failure=2;break;
        case 13:gpio_res[0].Data.FixedMemory32.Address=0x16004000;break;
        default:mux_res[0].Data.FixedMemory32.Address=0x4170000;break;
        }
        reject();
    }
    for(unsigned int which=0;which<2;which++) for(unsigned int fault=0;fault<9;fault++) {
        reset();ACPI_RESOURCE *r=which ? mux_res : gpio_res;
        switch(fault) {
        case 0:r[0].Data.FixedMemory32.AddressLength=0xfff;break;
        case 1:r[0].Data.FixedMemory32.WriteProtect=0;break;
        case 2:r[1]=r[0];break;case 3:r[0].Type=ACPI_RESOURCE_TYPE_ADDRESS64;break;
        case 4:r[2].Type=ACPI_RESOURCE_TYPE_VENDOR;break;
        case 5:if(which)mux_count=2;else gpio_count=2;break;
        case 6:r[0].Length=0;break;
        case 7:r[1].Length=0;break;
        default:r[2].Length=0;break;
        }
        reject();
    }
    for(unsigned int fault=0;fault<10;fault++) {
        reset();ACPI_RESOURCE_EXTENDED_IRQ *irq=&gpio_res[1].Data.ExtendedIrq;
        switch(fault) {
        case 0:irq->Interrupts[0]=404;break;case 1:irq->InterruptCount=0;break;
        case 2:irq->ProducerConsumer=0;break;case 3:irq->Triggering=1;break;
        case 4:irq->Polarity=1;break;case 5:irq->Shareable=1;break;
        case 6:irq->WakeCapable=1;break;case 7:irq->ResourceSource.StringLength=1;break;
        case 8:irq->InterruptCount=2;break;default:irq->ResourceSource.Index=1;break;
        }
        reject();
    }
    for(unsigned int fault=0;fault<19;fault++) {
        reset();ACPI_RESOURCE_PIN_GROUP *g=&mux_res[1].Data.PinGroup;
        switch(fault) {
        case 0:g->RevisionId=0;break;case 1:g->ProducerConsumer=1;break;
        case 2:g->PinTableLength=0;break;case 3:g->PinTableLength=33;break;
        case 4:g->PinTable=NULL;break;case 5:g->VendorLength=7;break;
        case 6:g->VendorData=NULL;break;case 7:g->ResourceLabel.StringLength=0;break;
        case 8:g->ResourceLabel.StringLength=65;break;case 9:g->ResourceLabel.StringPtr=NULL;break;
        case 10:fixture_label[5]='x';break;case 11:pins[0]=66;break;
        case 12:vendor[1]=0x81;break;case 13:vendor[2]=2;break;
        case 14:pins[1]=pins[0];break;case 15:mux_res[1].Type=ACPI_RESOURCE_TYPE_PIN_FUNCTION;break;
        case 16:fixture_label[1]='\0';break;case 17:fixture_label[0]=(char)0x80;break;
        default:vendor[0]=0xff;break;
        }
        reject();
    }
    reset();for(unsigned int i=1;i<34;i++)mux_res[i]=mux_res[1];
    mux_count=35;reject();
    reset();run_attach();unsigned int evaluations=evals;drop();
    for(unsigned int i=1;i<=evaluations;i++) {
        reset();eval_failure=i;run_attach();
        /* GetHandle has no injected error; all value-service errors must reject. */
        if(eval_injected) CHECK(reads==0);else drop();
    }
    for(unsigned int i=1;i<=2;i++) {
        reset();map_failure=i;run_attach();
        CHECK(maps==i && reads==0 && unmaps==i-1 && depth==0 && writes==0 && children==0);
    }
for(unsigned int i=0;i<4;i++) {
    reset();if(i<3)hw_mux[i]=0x144;else hw_bypass=4;
    run_attach();CHECK(writes==0 && children==0 && unmaps==2 && depth==0);
}
reset();readback_failure=true;run_attach();CHECK(writes==1 && barriers==1 && children==0 && unmaps==2 && depth==0);
CHECK(hw_direction==0xa5a50007); /* Never roll back to outputs. */
reset();child_failure=1;run_attach();CHECK(writes==1 && children==1 && unmaps==2 && depth==0);
CHECK(hw_direction==0xa5a50007);
#ifdef CIXGPIOINPUT_API
    CHECK(attachment[0]!=NULL && attachment[1]!=NULL);
#endif
    printf("cixgpio-input: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
