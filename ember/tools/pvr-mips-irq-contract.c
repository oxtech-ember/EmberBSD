/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD; exercise actual MIPS IRQ register access. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
typedef void *bus_space_tag_t;
typedef unsigned long bus_space_handle_t;
#define BUS_SPACE_BARRIER_WRITE 2
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static int reads,writes,barriers;
static uint32_t reg;
static uint32_t bus_space_read_4(void *t,bus_space_handle_t h,unsigned off)
{ CHECK(t && h==3 && off==0x8a8);reads++;return reg; }
static void bus_space_write_4(void *t,bus_space_handle_t h,unsigned off,uint32_t v)
{ CHECK(t && h==3 && off==0x8b0 && v==1);writes++; }
static void bus_space_barrier(void *t,bus_space_handle_t h,unsigned off,unsigned size,int flags)
{ CHECK(t && h==3 && off==0x8b0 && size==4 && flags==2);barriers++; }
#include "irq-actual.h"
int main(void) {
 bool pending=true; int tag;
 CHECK(pvr_mips_irq_pending(&tag,3,&pending)==0 && !pending && reads==1);
 reg=0x81;CHECK(pvr_mips_irq_pending(&tag,3,&pending)==0 && pending && reads==2);
 CHECK(pvr_mips_irq_clear(&tag,3)==0 && writes==1 && barriers==1);
 CHECK(pvr_mips_irq_pending(NULL,3,&pending)==EINVAL && pvr_mips_irq_clear(NULL,3)==EINVAL);
 puts("PowerVR MIPS IRQ actual register access: PASS");return 0;
}
