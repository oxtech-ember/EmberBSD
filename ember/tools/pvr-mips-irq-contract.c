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
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static int reads,writes,barriers;
static uint32_t reg, mask;
static bool stuck_mask;
static uint32_t bus_space_read_4(void *t,bus_space_handle_t h,unsigned off)
{ CHECK(t && h==3 && (off==0x8a8 || off==0x8a0));reads++;return off==0x8a8 ? reg : mask; }
static void bus_space_write_4(void *t,bus_space_handle_t h,unsigned off,uint32_t v)
{ CHECK(t && h==3 && ((off==0x8b0 && v==1) || (off==0x8a0 && v<=1)));
  if (off==0x8a0 && !stuck_mask) mask=v;
  writes++; }
static void bus_space_barrier(void *t,bus_space_handle_t h,unsigned off,unsigned size,int flags)
{ CHECK(t && h==3 && size==4 && ((off==0x8b0 && flags==2) || (off==0x8a0 && flags==3)));barriers++; }
#include "irq-actual.h"
int main(void) {
 bool pending=true; int tag;
 CHECK(pvr_mips_irq_pending(&tag,3,&pending)==0 && !pending && reads==1);
 reg=0x81;CHECK(pvr_mips_irq_pending(&tag,3,&pending)==0 && pending && reads==2);
 CHECK(pvr_mips_irq_clear(&tag,3)==0 && writes==1 && barriers==1);
 CHECK(pvr_mips_irq_pending(NULL,3,&pending)==EINVAL && pvr_mips_irq_clear(NULL,3)==EINVAL);
 CHECK(pvr_mips_irq_enable(&tag,3,true)==0 && mask==1);
 CHECK(pvr_mips_irq_enable(&tag,3,false)==0 && mask==0);
 stuck_mask=true;
 CHECK(pvr_mips_irq_enable(&tag,3,true)==EIO);
 CHECK(pvr_mips_irq_enable(NULL,3,true)==EINVAL);
 CHECK(reads==5 && writes==4 && barriers==4);
 puts("PowerVR MIPS IRQ actual register access: PASS");return 0;
}
