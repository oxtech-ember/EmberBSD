/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual attachment functions; native/ACPICA services are modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define ACPI_APPLICATION
#include <acpi.h>

#ifdef CIXGPIOOBS_API
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
struct test_cfdata { int cf_flags; };
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
static bool eval_injected;
static unsigned int stack[2], depth;
static bus_addr_t read_addresses[8];
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
    static const bus_addr_t allowed[]={0x16007080,0x16007084,0x16007088,
        0x16005000,0x16005004,0x16005010};
    static const uint32_t values[]={0x144,0,0x180,0x12,0x3ff,0x5};
    CHECK(tag==&handles && depth==2 && maps==2 && reads<6);
    unsigned int index=reads++;
    if(index>=6) return 0;
    read_addresses[index]=(bus_addr_t)handle+offset;
    CHECK(read_addresses[index]==allowed[index]);
    return values[index];
}
static __attribute__((unused)) void prohibited(const char *what) { (void)what; forbidden++;CHECK(false); }
#define bus_space_write_4(...) prohibited("write")
#define acpi_intr_establish_irq(...) (prohibited("IRQ"),(void *)NULL)
#define acpi_event_create_gpio(...) (prohibited("event"),AE_ERROR)

#ifdef CIXGPIOOBS_API
#include "cixgpio-observe-source.inc"
#else
static int cixgpioobs_acpi_match(device_t p,cfdata_t c,void *a)
{ (void)p;(void)c;(void)a;return 0; }
static void cixgpioobs_acpi_attach(device_t p,device_t s,void *a)
{ (void)p;(void)s;(void)a; }
#endif

static void reset(void)
{
    memset(gpio_res,0,sizeof(gpio_res));memset(mux_res,0,sizeof(mux_res));
    evals=live_strings=walks=maps=unmaps=reads=forbidden=depth=0;
    map_failure=eval_failure=walk_failure=0;eval_injected=false;
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
static void run_attach(void)
{ cixgpioobs_acpi_attach(NULL,&handles,&fixture_aa);CHECK(live_strings==0 && forbidden==0 && depth==0); }
static void reject(void)
{ run_attach();CHECK(maps==0 && reads==0 && unmaps==0); }
int main(void)
{
    reset();
    CHECK(cixgpioobs_acpi_match(NULL,&fixture_cf,&fixture_aa)>0);
    run_attach();CHECK(maps==2 && reads==6 && unmaps==2);
    CHECK(strstr(output,"mux80=0x144 mux84=0 mux88=0x180")!=NULL);
    CHECK(strstr(output,"bypass=0x12 direction=0x3ff input=0x5")!=NULL);
    for(unsigned int i=0;i<4;i++) {
        reset();fixture_cf.cf_flags=(int[]){0,2,3,-1}[i];
        CHECK(cixgpioobs_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();CHECK(evals==0 && walks==0);
    }
    reset();node.ad_handle=OTHER;CHECK(cixgpioobs_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();
    reset();compatible=false;CHECK(cixgpioobs_acpi_match(NULL,&fixture_cf,&fixture_aa)==0);reject();
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
    reset();run_attach();unsigned int evaluations=evals;
    for(unsigned int i=1;i<=evaluations;i++) {
        reset();eval_failure=i;run_attach();
        /* GetHandle has no injected error; all value-service errors must reject. */
        if(eval_injected) CHECK(reads==0);
    }
    for(unsigned int i=1;i<=2;i++) {
        reset();map_failure=i;run_attach();
        CHECK(maps==i && reads==0 && unmaps==i-1);
    }
#ifdef CIXGPIOOBS_API
    CHECK(attachment[0]!=NULL && attachment[1]!=NULL);
#endif
    printf("cixgpio-observe: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
