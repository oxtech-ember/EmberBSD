/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
typedef unsigned u_int;
typedef unsigned bus_addr_t;
typedef void *device_t;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h,l) ((UINT32_MAX >> (31-(h))) & (UINT32_MAX << (l)))
#define __SHIFTOUT(v,m) (((v)&(m)) >> __builtin_ctz(m))
/* Attachment-only fields stay present in the boundary types. */
// cppcheck-suppress unusedStructMember
struct clk_domain { void *priv; };
// cppcheck-suppress unusedStructMember
struct clk { const char *name; struct clk_domain *domain; };
struct syscon {
    uint32_t words[256];
    bool held, deny;
    unsigned writes, aper_reads, aper_writes, last_write_reg;
    uint32_t last_write_value;
};
static unsigned checks;
#define CHECK(v) do { checks++; if (!(v)) { fprintf(stderr,"FAIL %u at %u: %s\n",checks,(unsigned)__LINE__,#v);return 1;} } while (0)
static void syscon_lock(struct syscon *s) { assert(!s->held);s->held=true; }
static void syscon_unlock(struct syscon *s) { assert(s->held);s->held=false; }
static uint32_t syscon_read_4(struct syscon *s,bus_addr_t reg) {
    assert(s->held && reg < sizeof(s->words) && reg%4==0);
    if(reg==0x12c)s->aper_reads++;
    return s->words[reg/4];
}
static void syscon_write_4(struct syscon *s,bus_addr_t reg,uint32_t value) {
    assert(s->held && reg < sizeof(s->words) && reg%4==0);
    s->writes++;s->last_write_reg=reg;s->last_write_value=value;
    if(reg==8){assert(value==0xdf0d);s->words[3]=0;}
    else if(reg==4){assert(value==0x767b);s->words[3]=1;}
    else if(reg==0x12c){s->aper_writes++;if(!s->words[3]&&!s->deny)s->words[reg/4]=value;}
    else s->words[reg/4]=value;
}
/* ACTUAL_DRIVER */
int main(void) {
    struct syscon registers={0};
    struct zynq7000_clkc_softc controller={.sc_syscon=&registers,.sc_ps_clk_frequency=30000000};
    const struct {unsigned id;uint32_t mask;} usb[]={{28,0x4},{29,0x8}};
    /* ARM PLL 1.2 GHz, CPU divider two, CPU_1x is 100 MHz. */
    registers.words[0x100/4]=40U<<12;registers.words[0x120/4]=2U<<8;
    registers.words[0x108/4]=30U<<12;
    for(unsigned i=0;i<2;i++) {
        for(unsigned locked=0;locked<2;locked++) {
            registers.words[3]=locked;registers.words[0x12c/4]=0xa5a50301;
            registers.writes=registers.aper_reads=registers.aper_writes=0;
            CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[usb[i].id])==0);
            CHECK(registers.words[0x12c/4]==(0xa5a50301|usb[i].mask));
            CHECK(registers.words[3]==locked);CHECK(!registers.held);
            CHECK(registers.aper_reads>=2);CHECK(registers.aper_writes==1);
            CHECK(registers.writes==(locked?3U:2U));
            if(locked){CHECK(registers.last_write_reg==4);CHECK(registers.last_write_value==0x767b);}
            CHECK(zynq7000_clkc_clk_get_rate(&controller,&controller.sc_clk[usb[i].id])==100000000);
            unsigned writes=registers.writes;
            CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[usb[i].id])==0);
            CHECK(registers.writes==writes);CHECK(registers.words[3]==locked);
            registers.words[0x12c/4]=0xa5a50301;registers.deny=true;
            CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[usb[i].id])==EIO);
            CHECK(registers.words[0x12c/4]==0xa5a50301);CHECK(registers.words[3]==locked);CHECK(!registers.held);
            /* The real indirect driver call consumed this failure injection. */
            // cppcheck-suppress redundantAssignment
            registers.deny=false;
        }
    }
    /* Existing APER consumers must keep every unrelated gate bit. */
    const struct {unsigned id;uint32_t mask;} previous[]={
        {32,1U<<10},{33,1U<<11},{34,1U<<14},{35,1U<<15},
        {38,1U<<18},{39,1U<<19},{40,1U<<20},{41,1U<<21}};
    registers.words[3]=0;
    for(unsigned i=0;i<sizeof(previous)/sizeof(previous[0]);i++) {
        registers.words[0x12c/4]=0x8000000c;
        CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[previous[i].id])==0);
        CHECK(registers.words[0x12c/4]==(0x8000000c|previous[i].mask));CHECK(!registers.held);
    }
    registers.words[0x178/4]=0x80000001;
    CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[15])==0);
    CHECK(registers.words[0x178/4]==0x80000000);
    unsigned writes=registers.writes;
    CHECK(zynq7000_clkc_clk_enable(&controller,&controller.sc_clk[27])==ENXIO);
    CHECK(registers.writes==writes);
    puts("Zynq USB APER clock provider passed");printf("%u checks\n",checks);return 0;
}
