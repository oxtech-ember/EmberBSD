/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native stats sysctl consumer contract, AI-assisted. */
/* Execute the owning stats sysctl wrapper; allocator/lookup/driver are services. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lwp { void *l_cred; };
struct sysctlnode { void *sysctl_data; size_t sysctl_size; unsigned marker,sysctl_flags; uint64_t sysctl_qdata; };
#define KM_SLEEP 1
static unsigned checks, failures, allocations, releases, lookups, driver_calls;
static int driver_error, lookup_error, driver_lock, nested, lookup_depth;
static size_t text_size;
static int copy_error,auth_error,cli_grow,cli_alloc_fail;
static unsigned cli_reads,cli_displays,cli_errors,cli_allocations,cli_releases;
static size_t cli_display_size;
static void *live[2];
static char output[4096], inner_output[4096];
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"%d: %s\n",__LINE__,#x); } } while(0)
void *kmem_alloc(size_t, int);
void kmem_free(void *, size_t);
static int rtwce_stats(char *, size_t);
#include "native-sysctl-signature.inc"
static int sysctl_lookup(SYSCTLFN_ARGS);
#define MIN(a,b) ((a)<(b)?(a):(b))
#define KASSERT assert
#define DPRINTF(x) ((void)0)
#define rw_lock_held(p) ((void)(p),1)
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(p))
#endif
#define KAUTH_SYSTEM_SYSCTL 0
#define KAUTH_REQ_SYSTEM_SYSCTL_PRVT 1
#define KAUTH_REQ_SYSTEM_SYSCTL_MODIFY 2
static int sysctl_treelock;
static int kauth_authorize_system(void *cred,int kind,int action,void *a,void *b,void *c)
{ (void)cred;(void)kind;(void)action;(void)a;(void)b;(void)c;return auth_error; }
static int sysctl_copyout(struct lwp *l,const void *src,void *dst,size_t n)
{
    const unsigned char *from=src;
    unsigned char *to=dst;
    (void)l;
    if(copy_error)return copy_error;
    /* Model kernel copyout/kcopy as byte transfers, retaining visible reads
     * for analysis instead of the host libc bulk-copy model. No opaque
     * service, initialized padding or diagnostic exception is used. */
    for(size_t i=0;i<n;i++)to[i]=from[i];
    return 0;
}
static int sysctl_copyin(struct lwp *l,const void *src,void *dst,size_t n)
{ (void)l;memcpy(dst,src,n);return 0; }
static void rnd_add_data(void *p,const void *d,size_t n,int flags)
{ (void)p;(void)d;(void)n;(void)flags; }
#define malloc(n,type,flags) malloc(n)
#define free(p,type) free(p)
#define sysctl_lookup native_sysctl_lookup
/* The kernel ABI has two unused parameters. Mark the signature only; keep
 * the complete generic lookup body and all diagnostics unchanged. */
#define name name __attribute__((__unused__))
#define oname oname __attribute__((__unused__))
#include "native-sysctl-lookup.inc"
#undef name
#undef oname
#undef sysctl_lookup
#undef malloc
#undef free
#include "native-sysctl-stats.inc"
#include "native-sysctl-size.inc"

static int slot(void *p)
{ for(unsigned i=0;i<2;i++)if(live[i]==p)return (int)i;return -1; }
void *kmem_alloc(size_t len,int flags)
{
    CHECK(!driver_lock && len==4096 && flags==KM_SLEEP);
    void *p=malloc(len);assert(p);
    unsigned i=0;while(i<2 && live[i])i++;
    assert(i<2);live[i]=p;allocations++;
    memset(p,0xa5,len);return p;
}
void kmem_free(void *p,size_t len)
{
    int i=slot(p);
    CHECK(!driver_lock && p!=NULL && i>=0 && len==4096);
    if(i<0)return;
    live[i]=NULL;releases++;memset(p,0xdd,len);free(p);
}
static int rtwce_stats(char *buf,size_t len)
{
    CHECK(!driver_lock && slot(buf)>=0 && len==4096);
    driver_lock=1;driver_calls++;
    if(cli_grow && cli_reads==2)text_size++;
    if(!driver_error) {
        assert(text_size<len);memset(buf,'s',text_size);buf[text_size]=0;
    }
    driver_lock=0;return driver_error;
}
static int sysctl_lookup(SYSCTLFN_ARGS)
{
    lookups++;
    CHECK(!driver_lock && slot(rnode->sysctl_data)>=0);
    CHECK(rnode->marker==0x12345678 && rnode->sysctl_size==text_size+1);
    CHECK(strlen(rnode->sysctl_data)==text_size);
    if(nested && lookup_depth==0) {
        struct sysctlnode root={.sysctl_size=17,.marker=0x12345678,.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING};
        size_t size=sizeof(inner_output);void *outer=rnode->sysctl_data;
        lookup_depth++;
        CHECK(rtwce_sysctl_stats(NULL,0,inner_output,&size,NULL,0,NULL,NULL,&root)==0);
        lookup_depth--;
        CHECK(slot(outer)>=0 && root.sysctl_data==NULL && root.sysctl_size==17);
        CHECK(strlen(outer)==text_size);
    }
    if(lookup_error)return lookup_error;
    return native_sysctl_lookup(SYSCTLFN_CALL(rnode));
}
static int prog_sysctl(const int *name,unsigned namelen,void *p,size_t *sz,const void *newp,size_t newlen)
{
 struct sysctlnode root={.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING,.marker=0x12345678};
 size_t saved=*sz;cli_reads++;
 int e=rtwce_sysctl_stats(name,namelen,p,sz,newp,newlen,NULL,NULL,&root);
 e=native_size_status(e,saved,p,*sz);if(e){errno=e;return -1;}return 0;
}
static void *cli_malloc(size_t n) { if(cli_alloc_fail){errno=ENOMEM;return NULL;}void *p=malloc(n);assert(p);cli_allocations++;return p; }
static void cli_free(void *p) { cli_releases++;free(p); }
static void sysctlerror(int ignored) { (void)ignored;cli_errors++; }
#define DISPLAY_VALUE 1
static void display_string(const struct sysctlnode *node,const char *name,const void *data,size_t sz,int kind)
{ (void)node;(void)name;CHECK(kind==DISPLAY_VALUE && sz==text_size+1 && strlen(data)==text_size);cli_displays++;cli_display_size=sz; }
#define malloc cli_malloc
#define free cli_free
#include "native-sysctl-cli.inc"
#undef malloc
#undef free
static void test_actual_cli_growth(void)
{
 driver_error=lookup_error=0;nested=0;text_size=2000;cli_grow=1;
 cli_reads=cli_displays=cli_errors=cli_allocations=cli_releases=0;
 native_cli_string();CHECK(cli_reads==2 && cli_displays==1 && cli_errors==0 && cli_display_size==2002);
 CHECK(cli_allocations==1 && cli_releases==1 && !live[0] && !live[1]);
 const size_t stable[]={0,2000,4095};
 for(unsigned i=0;i<3;i++) {
  cli_grow=0;text_size=stable[i];cli_reads=cli_displays=cli_errors=cli_allocations=cli_releases=0;
  native_cli_string();CHECK(cli_reads==(text_size<1024?1U:2U) && cli_displays==1 && cli_errors==0);
  CHECK(cli_allocations==(text_size<1024?0U:1U) && cli_allocations==cli_releases);
 }
 cli_grow=0;cli_reads=cli_displays=cli_errors=cli_allocations=cli_releases=0;text_size=2000;cli_alloc_fail=1;
 native_cli_string();CHECK(cli_reads==1 && cli_errors==1 && cli_displays==0 && cli_allocations==0 && cli_releases==0);cli_alloc_fail=0;
}
static void test_read_and_generic_errors(void)
{
 struct sysctlnode root={.sysctl_size=17,.marker=0x12345678,.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING};
 char generic_text[24];memset(generic_text,'g',23);generic_text[23]=0;
 root.sysctl_data=generic_text;root.sysctl_size=4096;size_t generic_size=sizeof(output);
 CHECK(native_sysctl_lookup(NULL,0,output,&generic_size,NULL,0,NULL,NULL,&root)==0 && generic_size==24);
 root.sysctl_data=NULL;root.sysctl_size=17;
 text_size=4095;driver_error=lookup_error=0;nested=0;
 size_t size=4095;unsigned a=allocations,f=releases;
 CHECK(rtwce_sysctl_stats(NULL,0,output,&size,NULL,0,NULL,NULL,&root)==ENOMEM && size==4096);
 CHECK(allocations==a+1 && releases==f+1 && !live[0] && !live[1]);
 size=0;CHECK(rtwce_sysctl_stats(NULL,0,NULL,&size,NULL,0,NULL,NULL,&root)==0 && size==4096);
 text_size=23;size=24;
 CHECK(rtwce_sysctl_stats(NULL,0,output,&size,NULL,0,NULL,NULL,&root)==0 && size==24 && output[23]==0);
 for(unsigned shortbuf=0;shortbuf<2;shortbuf++) {
  copy_error=EFAULT;size=shortbuf?1:sizeof(output);size_t saved=size;
  CHECK(rtwce_sysctl_stats(NULL,0,output,&size,NULL,0,NULL,NULL,&root)==EFAULT && size==saved);
  copy_error=0;
 }
 size=1;CHECK(rtwce_sysctl_stats(NULL,0,output,&size,"x",1,NULL,NULL,&root)==EPERM && size==1);
 size=1;CHECK(rtwce_sysctl_stats(NULL,1,output,&size,NULL,0,NULL,NULL,&root)==EINVAL && size==1);
 struct lwp l={0};root.sysctl_flags|=CTLFLAG_PRIVATE;auth_error=EPERM;size=0;
 CHECK(rtwce_sysctl_stats(NULL,0,NULL,&size,NULL,0,NULL,&l,&root)==EPERM && size==0);
 auth_error=0;root.sysctl_flags&=~CTLFLAG_PRIVATE;
 size=0;CHECK(rtwce_sysctl_stats(NULL,0,NULL,&size,"",0,NULL,NULL,&root)==0 && size==24);
 CHECK(root.sysctl_data==NULL && root.sysctl_size==17 && !live[0] && !live[1]);
}
/* Model-service boundary: only the requested nonoverlapping bytes may move.
 * Driver buffers retain poison outside the formatted string. */
static void test_copyout_service(void)
{
 unsigned char src[]={0x31,0,0x7e,0xa5},dst[]={0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
 copy_error=0;
 CHECK(sysctl_copyout(NULL,src,dst+1,3)==0);
 CHECK(dst[0]==0xcc && dst[1]==0x31 && dst[2]==0 && dst[3]==0x7e && dst[4]==0xcc && dst[5]==0xcc);
 CHECK(src[0]==0x31 && src[1]==0 && src[2]==0x7e && src[3]==0xa5);
 CHECK(sysctl_copyout(NULL,src,dst+1,0)==0 && dst[1]==0x31 && dst[4]==0xcc);
 copy_error=EFAULT;
 CHECK(sysctl_copyout(NULL,src,dst+1,4)==EFAULT);
 CHECK(dst[0]==0xcc && dst[1]==0x31 && dst[2]==0 && dst[3]==0x7e && dst[4]==0xcc && dst[5]==0xcc);
 copy_error=0;
}
int main(void)
{
    const int driver_errors[]={0,EBUSY,EIO,ENOSPC};
    const int lookup_errors[]={0,EFAULT,ENOMEM,EPERM};
    const size_t sizes[]={0,23,4095};
    for(unsigned d=0;d<4;d++)for(unsigned q=0;q<4;q++)
    for(unsigned n=0;n<3;n++)for(unsigned query=0;query<2;query++) {
        struct sysctlnode root={.sysctl_size=17,.marker=0x12345678,.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING};
        unsigned a=allocations,f=releases,reads=lookups,calls=driver_calls;
        size_t size=sizeof(output);text_size=sizes[n];
        driver_error=driver_errors[d];lookup_error=lookup_errors[q];
        int error=rtwce_sysctl_stats(NULL,0,query?NULL:output,&size,NULL,0,NULL,NULL,&root);
        CHECK(error==(driver_error?driver_error:lookup_error));
        CHECK(allocations==a+1 && releases==f+1 && driver_calls==calls+1);
        CHECK(lookups==reads+!driver_error);
        CHECK(!live[0] && !live[1] && !driver_lock);
        CHECK(root.sysctl_data==NULL && root.sysctl_size==17 && root.marker==0x12345678);
        if(!error) { CHECK(size==(query?4096:text_size+1));if(!query)CHECK(output[text_size]==0); }
    }
    driver_error=lookup_error=0;text_size=4095;nested=1;
    struct sysctlnode root={.sysctl_size=17,.marker=0x12345678,.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING};size_t size=sizeof(output);
    unsigned a=allocations,f=releases;
    CHECK(rtwce_sysctl_stats(NULL,0,output,&size,NULL,0,NULL,NULL,&root)==0);
    CHECK(allocations==a+2 && releases==f+2 && !live[0] && !live[1]);
    CHECK(size==4096 && memcmp(output,inner_output,4096)==0);
    test_actual_cli_growth();test_read_and_generic_errors();test_copyout_service();
    printf("Actual stats sysctl: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
