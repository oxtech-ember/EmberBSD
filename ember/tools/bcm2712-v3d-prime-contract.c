/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD. Exercise the actual native mmap with adversarial bounds. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#define PAGE_SIZE 4096
struct uvm_object { int id; };
struct drm_gem_object { size_t size; unsigned refs; struct uvm_object gemo_uvmobj; };
static void drm_gem_object_get(struct drm_gem_object *obj) { obj->refs++; }
#include "body.h"
int main(void)
{
 struct drm_gem_object obj={8192,1,{5}};
 struct uvm_object *uobj;
 const off_t offsets[]={INT64_MIN,-4096,-1,0,1,4095,4096,8192,INT64_MAX};
 const size_t sizes[]={0,1,4095,4096,8192,12288,SIZE_MAX};
 const int modes[]={0,MAP_SHARED,MAP_PRIVATE,MAP_SHARED|MAP_PRIVATE};
 const int protections[]={0,PROT_READ,PROT_WRITE,PROT_READ|PROT_WRITE,PROT_EXEC};
 unsigned count=0;
 for(unsigned a=0;a<sizeof(offsets)/sizeof(offsets[0]);a++)
 for(unsigned b=0;b<sizeof(sizes)/sizeof(sizes[0]);b++)
 for(unsigned c=0;c<sizeof(modes)/sizeof(modes[0]);c++)
 for(unsigned d=0;d<sizeof(protections)/sizeof(protections[0]);d++) {
  off_t off=offsets[a]; size_t len=sizes[b];
  int flags=modes[c],maxprot=42,advice=17,prot=protections[d];
  int valid=(off==0 || off==4096) && (len==4096 || len==8192) &&
    (uint64_t)off+len<=8192 && flags==MAP_SHARED && prot!=PROT_EXEC;
  obj.refs=1; uobj=NULL;
  int ret=bcmv3d_prime_mmap(&obj,&off,len,prot,&flags,&advice,&uobj,&maxprot);
  assert(off==offsets[a] && flags==modes[c] && advice==17);
  if(valid) { assert(ret==0 && obj.refs==2 && uobj==&obj.gemo_uvmobj);
    assert(maxprot==(PROT_READ|PROT_WRITE)); }
  else { assert(ret==-EINVAL && obj.refs==1 && uobj==NULL && maxprot==42); }
  count++;
 }
 printf("PASS: %u native PRIME offset/length/protection/reference cases\n",count);
 return 0;
}
