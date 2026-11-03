/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; profile-specific PowerVR compute stream contract. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sun60i_a733_gpu_fwcompute_cmd.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void p32(uint8_t *p,uint32_t x) { for (int i=0;i<4;i++) p[i]=(uint8_t)(x>>(i*8)); }
static void p64(uint8_t *p,uint64_t x) { p32(p,x);p32(p+4,x>>32); }
int main(void) {
 uint8_t stream[48]={0};struct rogue_fwif_cmd_compute out;
 uint64_t va=UINT64_C(0x8000004000), resume=UINT64_C(0x8000008000), border=UINT64_C(0x800000c000);
 p32(stream,36);p64(stream+8,border>>2);p64(stream+16,va);p64(stream+24,resume);
 p32(stream+32,0x02100002);p32(stream+36,1);p32(stream+40,0x20);
 CHECK(sun60i_a733_gpu_fwcompute_cmd_decode(stream,sizeof(stream),UINT64_C(0x0016006600360026),va,resume,border,&out)==EOPNOTSUPP);
 CHECK(pvr_fwcompute_cmd_decode(stream,sizeof(stream),UINT64_C(0x0016006600360026),va,resume,border,&out)==0);
 CHECK(out.regs.tpu==0x20 && out.regs.cdm_ctrl_stream_base==va);
 {
  struct rogue_fwif_cmd_compute saved=out;
  p32(stream+36,0);CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_GE8300_BVNC,va,resume,border,&out)==EOPNOTSUPP && memcmp(&out,&saved,sizeof(out))==0);
  p32(stream+36,1);p32(stream+40,0x21);CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_GE8300_BVNC,va,resume,border,&out)==EOPNOTSUPP && memcmp(&out,&saved,sizeof(out))==0);
  p32(stream+40,0x20);p32(stream+44,2);CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_GE8300_BVNC,va,resume,border,&out)==EINVAL && memcmp(&out,&saved,sizeof(out))==0);
  p32(stream+44,0);CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_A733_BVNC,va,resume,border,&out)==EINVAL && memcmp(&out,&saved,sizeof(out))==0);
  CHECK(pvr_fwcompute_cmd_decode(stream,48,0,va,resume,border,&out)==EOPNOTSUPP && memcmp(&out,&saved,sizeof(out))==0);
  p32(stream,44);p32(stream+36,0);p32(stream+40,0);
  CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_A733_BVNC,va,resume,border,&out)==0 && out.regs.tpu==0);
  saved=out;CHECK(pvr_fwcompute_cmd_decode(stream,48,PVR_FWCOMPUTE_GE8300_BVNC,va,resume,border,&out)==EINVAL && memcmp(&out,&saved,sizeof(out))==0);
 }
 puts("PowerVR profile-specific compute stream: PASS");return 0;
}
