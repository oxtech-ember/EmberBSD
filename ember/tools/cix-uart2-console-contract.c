/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual gate/consumer; native mapping, SHA and PL011 services are modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Signed constant count matches the existing consumer's signed loop variable. */
#define __arraycount(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define __CTASSERT(x) _Static_assert(x, #x)
#define __UNCONST(p) ((void *)(uintptr_t)(p))
#define PAGE_SIZE 4096
#define le64toh(x) (x)
#define le16toh(x) (x)
#define le32toh(x) (x)
typedef unsigned int u_int;
typedef int ACPI_STATUS;
typedef uint64_t ACPI_PHYSICAL_ADDRESS;
typedef uint32_t UINT32;
#define AE_OK 0
#define AE_NOT_FOUND 1
#define AE_ERROR 2
#define ACPI_SUCCESS(s) ((s) == AE_OK)
#define ACPI_FAILURE(s) ((s) != AE_OK)
#define ACPI_SIG_SPCR "SPCR"
#define ACPI_SIG_FADT "FACP"
#define ACPI_ADR_SPACE_SYSTEM_MEMORY 0
#define ACPI_DBG2_ARM_PL011 3
#define ACPI_DBG2_ARM_SBSA_32BIT 13
#define ACPI_DBG2_ARM_SBSA_GENERIC 14
#define SPCR_BAUD_115200 7
#define ACPI_FADT_PSCI_COMPLIANT 1
#define ACPI_FADT_PSCI_USE_HVC 2
#define NCOM 0
#define NPLCOM 1
#define PLCOM_TYPE_PL011 1
#define PLCOM_TYPE_GENERIC_UART 2
#define PLC_FLAG_32BIT_ACCESS 1
#define PL011COM_UART_SIZE 4096
#define TTYDEF_CFLAG 0x300
#define TOCONS 1
#define NOTSTAMP 2
typedef struct __attribute__((__packed__)) {
    uint8_t Signature[4]; uint32_t Length; uint8_t Revision; uint8_t rest[27];
} ACPI_TABLE_HEADER;
typedef struct __attribute__((__packed__)) {
    uint8_t prefix[40]; uint32_t Dsdt; uint8_t middle[96]; uint64_t XDsdt;
    uint16_t ArmBootFlags;
} ACPI_TABLE_FADT;
typedef struct __attribute__((__packed__)) {
    ACPI_TABLE_HEADER Header;
    uint8_t InterfaceType, Reserved[3];
    struct __attribute__((__packed__)) {
        uint8_t SpaceId, BitWidth, BitOffset, AccessWidth; uint64_t Address;
    } SerialPort;
    uint8_t InterruptType, PcInterrupt; uint32_t Interrupt;
    uint8_t BaudRate, Parity, StopBits, FlowControl, TerminalType, Language;
    uint8_t tail[16];
} ACPI_TABLE_SPCR;
_Static_assert(sizeof(ACPI_TABLE_SPCR)==80,"modeled SPCR prefix");
struct plcom_instance {
    int pi_type, pi_flags;
    void *pi_iot;
    uint64_t pi_iobase;
    size_t pi_size;
};
static int arm_generic_bs_tag;
static struct plcom_instance plcom_console;
static const struct { unsigned int id; int baud_rate; } acpi_spcr_baud_rates[] = {
    {7,115200}
};
static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL line %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)

static uint8_t rsdp[36], xsdt[60], fadt[276], dsdt[81063], reference[81063];
static uint8_t other[36];
static struct { uint64_t pa; uint8_t *bytes; size_t size; } regions[] = {
    {0x1000,rsdp,sizeof(rsdp)}, {0x2000,xsdt,sizeof(xsdt)},
    {0x3000,fadt,sizeof(fadt)}, {0x4000,dsdt,sizeof(dsdt)},
    {0x20000,other,sizeof(other)}, {0x30000,dsdt,sizeof(dsdt)}
};
static struct { void *pointer; uint32_t length; } active_maps[16];
static unsigned int maps, unmaps, live, fail_at, attaches, hashes, lookups;
static uint64_t root_pa;
static size_t largest_map;
static int spcr_status;
static ACPI_TABLE_SPCR stock_spcr;
static ACPI_TABLE_SPCR *present_spcr;
static bool hash_bad, change_prefix;
static size_t dsdt_len, hashed_len;
static void put(uint8_t *p,uint64_t value,unsigned int n)
{ for (unsigned int i=0;i<n;i++) p[i]=(uint8_t)(value>>(8*i)); }
static void checksum(uint8_t *p,size_t n,size_t offset)
{
    unsigned int sum=0; p[offset]=0;
    for(size_t i=0;i<n;i++) sum+=p[i];
    p[offset]=(uint8_t)(0-sum);
}
static void header(uint8_t *p,const char *sig,size_t n)
{ memcpy(p,sig,4);put(p+4,n,4);p[8]=2;checksum(p,n,9); }
static void init(void)
{
    memset(rsdp,0,sizeof(rsdp));memset(xsdt,0,sizeof(xsdt));
    memset(fadt,0,sizeof(fadt));memset(dsdt,0,sizeof(dsdt));
    memset(other,0,sizeof(other));
    memcpy(rsdp,"RSD PTR ",8);rsdp[15]=2;put(rsdp+20,36,4);put(rsdp+24,0x2000,8);
    checksum(rsdp,20,8);checksum(rsdp,36,32);
    put(xsdt+36,0x3000,8);header(xsdt,"XSDT",44);
    put(fadt+40,0x4000,4);put(fadt+140,0x4000,8);header(fadt,"FACP",sizeof(fadt));
    dsdt_len=sizeof(dsdt);header(dsdt,"DSDT",dsdt_len);
    memcpy(reference,dsdt,sizeof(dsdt));hashed_len=0;
    header(other,"SPCR",sizeof(other));
    root_pa=0x1000;maps=unmaps=live=fail_at=attaches=hashes=0;largest_map=0;
    lookups=0;regions[3].pa=0x4000;regions[0].size=sizeof(rsdp);
    change_prefix=false;
    regions[4].bytes=other;regions[4].size=sizeof(other);
    hash_bad=false;spcr_status=AE_NOT_FOUND;
    memset(&stock_spcr,0,sizeof(stock_spcr));
    present_spcr=&stock_spcr;
    stock_spcr.SerialPort.Address=0x040e0000;stock_spcr.BaudRate=7;
    stock_spcr.InterfaceType=ACPI_DBG2_ARM_PL011;
    stock_spcr.SerialPort.BitWidth=32;stock_spcr.SerialPort.AccessWidth=3;
    stock_spcr.InterruptType=8;stock_spcr.Interrupt=330;stock_spcr.StopBits=1;
    header((uint8_t *)&stock_spcr,"SPCR",80);
}
static ACPI_PHYSICAL_ADDRESS acpi_md_OsGetRootPointer(void) { return root_pa; }
static ACPI_STATUS acpi_md_OsMapMemory(uint64_t pa,UINT32 len,void **out)
{
    maps++;if(len>largest_map)largest_map=len;
    if(fail_at==maps)return AE_ERROR;
    if(change_prefix && pa==0x1000 && len==36) {
        rsdp[10]^=1;checksum(rsdp,20,8);checksum(rsdp,36,32);
    }
    for(int i=0;i<__arraycount(regions);i++) {
        if(pa==regions[i].pa && len<=regions[i].size) {
            CHECK(live<(unsigned int)__arraycount(active_maps));
            if(live>=(unsigned int)__arraycount(active_maps))return AE_ERROR;
            *out=regions[i].bytes;active_maps[live].pointer=*out;
            active_maps[live].length=len;live++;return AE_OK;
        }
    }
    return AE_ERROR;
}
static void acpi_md_OsUnmapMemory(void *p,UINT32 len)
{
    CHECK(p!=NULL && len>0 && live>0);
    if(live==0)return;
    CHECK(active_maps[live-1].pointer==p && active_maps[live-1].length==len);
    unmaps++;live--;
}
/* The native SHA service is not reimplemented. Check exact synthetic input. */
typedef struct { unsigned int updated; } SHA256_CTX;
static const uint8_t accepted_digest[32]={
    0x9e,0x7b,0xf3,0x0c,0xb2,0x94,0x84,0x91,0x0c,0xce,0xec,0x20,0x8a,0xdf,0x6f,0xa9,
    0xf1,0xfa,0xb4,0xed,0x54,0xc9,0x32,0x36,0xd5,0x3b,0xb4,0x11,0x9b,0x04,0x26,0x0a};
/* BiosV1.4-W20260130-97268c4c: 77645-byte stock DSDT. */
static const uint8_t bios14_digest[32]={
    0xf2,0xf6,0xdd,0x34,0xf1,0x6e,0x3b,0x5f,0x04,0xdd,0xbf,0xee,0xa1,0x9b,0x45,0x66,
    0x05,0x84,0x0b,0xbf,0x2e,0xf9,0x9f,0x8c,0x95,0x59,0x3b,0x38,0x87,0x7e,0x42,0x39};
#define SHA256_DIGEST_LENGTH 32
static void SHA256_Init(SHA256_CTX *c) { c->updated=0; }
static void SHA256_Update(SHA256_CTX *c,const void *p,size_t len)
{ CHECK(len==dsdt_len); c->updated=memcmp(p,reference,len)==0?1:2;hashed_len=len;hashes++; }
/* Model each exact synthetic image as the stock image of its size. */
static void SHA256_Final(uint8_t *out,SHA256_CTX *c)
{
    memcpy(out,hashed_len==77645?bios14_digest:accepted_digest,32);
    if(c->updated!=1||hash_bad)out[0]^=1;
}
static void set_dsdt(size_t n)
{
    memset(dsdt,0,sizeof(dsdt));dsdt_len=n;header(dsdt,"DSDT",n);
    memcpy(reference,dsdt,sizeof(dsdt));
}

#ifdef CIX_UART2_GATE
#include "gate.inc"
#else
static int cix_uart2_console_probe(void) { return ENOTSUP; }
#endif
static ACPI_STATUS acpi_table_find(const char *sig,void **out)
{
    if(strcmp(sig,"SPCR")==0) {
        lookups++;
#ifdef CIXP1_UART2_CONSOLE
        CHECK(maps>0); /* Closed preflight precedes the legacy lookup. */
#endif
    }
    if(strcmp(sig,"SPCR")==0 && spcr_status==AE_OK)*out=present_spcr;
    return strcmp(sig,"SPCR")==0?spcr_status:AE_NOT_FOUND;
}
static void acpi_table_unmap(ACPI_TABLE_HEADER *p) { CHECK(p!=NULL); }
static int plcomcnattach(struct plcom_instance *p,int baud,int freq,int flags,int unit)
{
    attaches++;CHECK(baud==115200 && freq==0 && flags==TTYDEF_CFLAG && unit==-1);
    CHECK(p->pi_iot==&arm_generic_bs_tag && p->pi_type==PLCOM_TYPE_PL011);
    CHECK(p->pi_iobase==(spcr_status==AE_OK?present_spcr->SerialPort.Address:0x040d0000));
    CHECK(p->pi_size==4096 && p->pi_flags==PLC_FLAG_32BIT_ACCESS);return 0;
}
static void psci_init(void *p) { (void)p;CHECK(false); }
#define psci_call_hvc NULL
#define psci_call_smc NULL
static void smccc_probe(void) { CHECK(false); }
static void printf_flags(int f,const char *s) { (void)f;(void)s; }
#include "consumer.inc"

int main(void)
{
    /* Keep service imports available in the missing-implementation RED. */
    (void)acpi_md_OsGetRootPointer;(void)acpi_md_OsMapMemory;
    (void)acpi_md_OsUnmapMemory;(void)SHA256_Init;(void)SHA256_Update;
    (void)SHA256_Final;
    init();CHECK(cix_uart2_console_probe()==0);CHECK(live==0 && hashes==1);
    unsigned int successes=maps;
    for(unsigned int i=1;i<=successes;i++) {
        init();fail_at=i;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    }
    init();root_pa=UINT64_MAX-15;CHECK(cix_uart2_console_probe()!=0);CHECK(maps==0 && live==0);
    init();root_pa=UINT64_MAX-100;CHECK(cix_uart2_console_probe()!=0);CHECK(maps==0 && live==0);
    init();rsdp[8]^=1;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();rsdp[15]=0;checksum(rsdp,20,8);regions[0].size=20;
    CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=20 && live==0);
    init();change_prefix=true;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();xsdt[9]^=1;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(xsdt+4,36+8*257,4);CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=36 && live==0);
    init();put(fadt+4,65537,4);CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=44 && live==0);
    init();put(fadt+4,4097,4);CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=44 && live==0);
    init();put(fadt+140,UINT64_MAX-20,8);put(fadt+40,0,4);checksum(fadt,sizeof(fadt),9);
    CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(fadt+140,0x20000,8);checksum(fadt,sizeof(fadt),9);
    CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(fadt+140,0x30000,8);checksum(fadt,sizeof(fadt),9);
    CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(fadt+140,0,8);checksum(fadt,sizeof(fadt),9);CHECK(cix_uart2_console_probe()==0);CHECK(live==0);
    init();put(fadt+40,0,4);checksum(fadt,sizeof(fadt),9);CHECK(cix_uart2_console_probe()==0);CHECK(live==0);
    init();fadt[9]^=1;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(dsdt+4,131073,4);CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=276 && live==0);
    init();put(dsdt+4,81062,4);checksum(dsdt,81062,9);
    CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=276 && live==0);
    init();put(fadt+40,0,4);put(fadt+140,UINT64_MAX-4096,8);checksum(fadt,sizeof(fadt),9);
    regions[3].pa=UINT64_MAX-4096;
    CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=276 && live==0);
    init();dsdt[9]^=1;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();dsdt[40]=1;checksum(dsdt,sizeof(dsdt),9);CHECK(cix_uart2_console_probe()!=0);CHECK(hashes==1 && live==0);
    init();hash_bad=true;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(xsdt+44,0x20000,8);header(xsdt,"XSDT",52);CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(xsdt+44,0x3000,8);header(xsdt,"XSDT",52);CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(xsdt+36,0x20000,8);header(other,"TEST",36);checksum(xsdt,44,9);
    CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    init();put(fadt+4,147,4);CHECK(cix_uart2_console_probe()!=0);CHECK(largest_map<=44 && live==0);
    /* BiosV1.4 profile: exact size and digest, selected before full mapping. */
    init();set_dsdt(77645);CHECK(cix_uart2_console_probe()==0);
    CHECK(hashes==1 && hashed_len==77645 && largest_map==77645 && live==0);
    unsigned int bios14_maps=maps;
    for(unsigned int i=1;i<=bios14_maps;i++) {
        init();set_dsdt(77645);fail_at=i;CHECK(cix_uart2_console_probe()!=0);CHECK(live==0);
    }
    init();set_dsdt(77645);hash_bad=true;
    CHECK(cix_uart2_console_probe()==ENOTSUP);CHECK(hashes==1 && live==0);
    init();set_dsdt(77645);dsdt[40]=1;checksum(dsdt,77645,9);
    CHECK(cix_uart2_console_probe()==ENOTSUP);CHECK(hashes==1 && live==0);
    init();set_dsdt(77645);dsdt[9]^=1;CHECK(cix_uart2_console_probe()!=0);CHECK(hashes==0 && live==0);
    size_t near[]={77644,77646,81062,81064};
    for(unsigned int i=0;i<sizeof(near)/sizeof(near[0]);i++) {
        init();set_dsdt(near[i]>sizeof(dsdt)?sizeof(dsdt):near[i]);put(dsdt+4,near[i],4);
        checksum(dsdt,near[i]>sizeof(dsdt)?sizeof(dsdt):near[i],9);
        CHECK(cix_uart2_console_probe()==EPROTO);CHECK(hashes==0 && largest_map<=276 && live==0);
    }
    init();acpi_platform_startup();
#ifdef CIXP1_UART2_CONSOLE
    CHECK(attaches==1 && live==0);
#else
    CHECK(attaches==0 && maps==0);
#endif
    init();hash_bad=true;acpi_platform_startup();CHECK(attaches==0 && live==0);
    init();set_dsdt(77645);acpi_platform_startup();
#ifdef CIXP1_UART2_CONSOLE
    CHECK(attaches==1 && hashes==1 && live==0);
#else
    CHECK(attaches==0 && maps==0);
#endif
    init();set_dsdt(77645);hash_bad=true;acpi_platform_startup();CHECK(attaches==0 && live==0);
    init();spcr_status=AE_ERROR;acpi_platform_startup();CHECK(attaches==0 && live==0);
    init();spcr_status=AE_OK;acpi_platform_startup();
#ifdef CIXP1_UART2_CONSOLE
    CHECK(attaches==0 && live==0); /* Stock UART3 belongs to an instrument. */
#else
    CHECK(attaches==1 && live==0);
#endif
#ifdef CIXP1_UART2_CONSOLE
    for(unsigned int i=1;i<=successes;i++) {
        init();fail_at=i;acpi_platform_startup();CHECK(attaches==0 && lookups==0 && live==0);
    }
    init();put(xsdt+44,0x20000,8);header(xsdt,"XSDT",52);
    spcr_status=AE_OK;hash_bad=true;stock_spcr.SerialPort.Address=0x040d0000;
    checksum((uint8_t *)&stock_spcr,80,9);
    regions[4].bytes=(uint8_t *)&stock_spcr;regions[4].size=80;
    acpi_platform_startup();
    CHECK(attaches==1 && hashes==0 && live==0);
    for(unsigned int bad=0;bad<16;bad++) {
        init();spcr_status=AE_OK;stock_spcr.SerialPort.Address=0x040d0000;
        switch(bad) {
        case 0:stock_spcr.SerialPort.SpaceId=1;break;
        case 1:stock_spcr.SerialPort.BitWidth=8;break;
        case 2:stock_spcr.SerialPort.BitOffset=1;break;
        case 3:stock_spcr.SerialPort.AccessWidth=1;break;
        case 4:stock_spcr.BaudRate=6;break;
        case 5:stock_spcr.Parity=1;break;
        case 6:stock_spcr.StopBits=2;break;
        case 7:stock_spcr.InterfaceType=ACPI_DBG2_ARM_SBSA_GENERIC;break;
        case 8:stock_spcr.Interrupt=331;break;
        case 9:stock_spcr.Header.Length=79;break;
        case 10:stock_spcr.Header.Revision=1;break;
        case 11:stock_spcr.InterruptType=1;break;
        case 12:stock_spcr.FlowControl=1;break;
        case 13:stock_spcr.SerialPort.Address=0x040c0000;break;
        case 14:stock_spcr.Header.Length=81;break;
        default:stock_spcr.Header.Signature[0]='X';break;
        }
        checksum((uint8_t *)&stock_spcr,80,9);acpi_platform_startup();
        CHECK(attaches==0 && live==0);
    }
    init();spcr_status=AE_OK;stock_spcr.SerialPort.Address=0x040d0000;
    stock_spcr.Header.rest[0]^=1;acpi_platform_startup();CHECK(attaches==0);
    init();spcr_status=AE_OK;
    uint8_t short_spcr[36]={0};put(short_spcr+4,36,4);
    present_spcr=(ACPI_TABLE_SPCR *)(void *)short_spcr;
    acpi_platform_startup();CHECK(attaches==0 && live==0);
#endif
    printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
