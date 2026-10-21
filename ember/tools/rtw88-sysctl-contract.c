/* SPDX-License-Identifier: BSD-2-Clause */
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lwp;
struct sysctlnode { void *sysctl_data; size_t sysctl_size; unsigned marker; };
#define KM_SLEEP 1
static unsigned checks, failures, allocations, releases, lookups, driver_calls;
static int driver_error, lookup_error, driver_lock, nested, lookup_depth;
static size_t text_size;
static void *live[2];
static char output[4096], inner_output[4096];
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"%d: %s\n",__LINE__,#x); } } while(0)
void *kmem_alloc(size_t, int);
void kmem_free(void *, size_t);
static int rtwce_stats(char *, size_t);
#include "native-sysctl-signature.inc"
static int sysctl_lookup(SYSCTLFN_ARGS);
#include "native-sysctl-stats.inc"

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
    if(!driver_error) {
        assert(text_size<len);memset(buf,'s',text_size);buf[text_size]=0;
    }
    driver_lock=0;return driver_error;
}
static int sysctl_lookup(SYSCTLFN_ARGS)
{
    (void)name;(void)namelen;(void)newp;(void)newlen;(void)oname;(void)l;
    lookups++;
    CHECK(!driver_lock && slot(rnode->sysctl_data)>=0);
    CHECK(rnode->marker==0x12345678 && rnode->sysctl_size==text_size+1);
    CHECK(strlen(rnode->sysctl_data)==text_size);
    if(nested && lookup_depth==0) {
        struct sysctlnode root={NULL,17,0x12345678};
        size_t size=sizeof(inner_output);void *outer=rnode->sysctl_data;
        lookup_depth++;
        CHECK(rtwce_sysctl_stats(NULL,0,inner_output,&size,NULL,0,NULL,NULL,&root)==0);
        lookup_depth--;
        CHECK(slot(outer)>=0 && root.sysctl_data==NULL && root.sysctl_size==17);
        CHECK(strlen(outer)==text_size);
    }
    if(lookup_error)return lookup_error;
    if(oldp && oldlenp) {
        size_t len=*oldlenp<rnode->sysctl_size?*oldlenp:rnode->sysctl_size;
        memcpy(oldp,rnode->sysctl_data,len);
    }
    if(oldlenp)*oldlenp=rnode->sysctl_size;
    return 0;
}
int main(void)
{
    const int driver_errors[]={0,EBUSY,EIO,ENOSPC};
    const int lookup_errors[]={0,EFAULT,ENOMEM,EPERM};
    const size_t sizes[]={0,23,4095};
    for(unsigned d=0;d<4;d++)for(unsigned q=0;q<4;q++)
    for(unsigned n=0;n<3;n++)for(unsigned query=0;query<2;query++) {
        struct sysctlnode root={NULL,17,0x12345678};
        unsigned a=allocations,f=releases,reads=lookups,calls=driver_calls;
        size_t size=sizeof(output);text_size=sizes[n];
        driver_error=driver_errors[d];lookup_error=lookup_errors[q];
        int error=rtwce_sysctl_stats(NULL,0,query?NULL:output,&size,NULL,0,NULL,NULL,&root);
        CHECK(error==(driver_error?driver_error:lookup_error));
        CHECK(allocations==a+1 && releases==f+1 && driver_calls==calls+1);
        CHECK(lookups==reads+!driver_error);
        CHECK(!live[0] && !live[1] && !driver_lock);
        CHECK(root.sysctl_data==NULL && root.sysctl_size==17 && root.marker==0x12345678);
        if(!error) { CHECK(size==text_size+1);if(!query)CHECK(output[text_size]==0); }
    }
    driver_error=lookup_error=0;text_size=4095;nested=1;
    struct sysctlnode root={NULL,17,0x12345678};size_t size=sizeof(output);
    unsigned a=allocations,f=releases;
    CHECK(rtwce_sysctl_stats(NULL,0,output,&size,NULL,0,NULL,NULL,&root)==0);
    CHECK(allocations==a+2 && releases==f+2 && !live[0] && !live[1]);
    CHECK(size==4096 && memcmp(output,inner_output,4096)==0);
    printf("Actual stats sysctl: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
