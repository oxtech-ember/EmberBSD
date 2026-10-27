/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD bounded packet ownership and external-interlock contracts. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dev/ic/rtw88_async.h>

static atomic_uint checks, failures;
#define CHECK(c) do { \
    atomic_fetch_add_explicit(&checks, 1, memory_order_relaxed); \
    if (!(c)) { \
        atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed); \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); \
    } \
} while (0)
static const struct rtw88_async_epoch initial = {1, 1, 1};

static struct rtw88_async *
new_queue(void)
{
    struct rtw88_async *a=calloc(1,sizeof(*a));
    if(a==NULL)abort();
    rtw88_async_init(a,&initial);
    return a;
}

static void
bounds_and_control(void)
{
    struct rtw88_async *a=new_queue();
    struct rtw88_async_ticket tickets[64], unchanged={99,99};
    struct rtw88_tx_job *job;
    for(unsigned i=0;i<56;i++) {
        CHECK(rtw88_async_reserve(a,&initial,1+i/16,false,&tickets[i],&job)==0);
        CHECK(a->total==i+1 && a->data==i+1);
        for(unsigned q=0;q<RTW88_NTXQ;q++)CHECK(a->count[q]<=16);
    }
    struct rtw88_async_ticket out=unchanged;
    struct rtw88_tx_job *untouched=(void *)(uintptr_t)1;
    CHECK(rtw88_async_reserve(a,&initial,RTW88_TXQ_VO,false,&out,&untouched)==ENOBUFS);
    CHECK(out.slot==99 && out.serial==99 && untouched==(void *)(uintptr_t)1);
    for(unsigned i=56;i<64;i++)
        CHECK(rtw88_async_reserve(a,&initial,RTW88_TXQ_MGMT,true,&tickets[i],&job)==0);
    CHECK(a->total==64 && a->data==56 && a->count[0]==8);
    CHECK(rtw88_async_reserve(a,&initial,0,true,&out,&untouched)==ENOBUFS);
    a->wake_mask=0;
    CHECK(rtw88_async_cancel(a,&tickets[63])==0);
    CHECK((a->wake_mask&1U)!=0);
    CHECK((a->wake_mask&(1U<<RTW88_TXQ_VO))==0); /* Data quota is still full. */
    a->wake_mask=0;
    CHECK(rtw88_async_cancel(a,&tickets[0])==0);
    CHECK((a->wake_mask&(1U<<RTW88_TXQ_VO))!=0);
    for(unsigned i=1;i<63;i++)CHECK(rtw88_async_cancel(a,&tickets[i])==0);
    CHECK(a->total==0 && a->data==0);
    free(a);
}

static void
low_water_and_invalid_owner(void)
{
    struct rtw88_async *a=new_queue();
    struct rtw88_async_ticket t[16], out={77,88};
    struct rtw88_tx_job *job;
    for(unsigned i=0;i<16;i++)CHECK(rtw88_async_reserve(a,&initial,1,false,&t[i],&job)==0);
    CHECK(rtw88_async_reserve(a,&initial,1,false,&out,&job)==ENOBUFS);
    a->wake_mask=0;
    for(unsigned i=0;i<7;i++)CHECK(rtw88_async_cancel(a,&t[i])==0);
    CHECK(a->wake_mask==0 && a->count[1]==9);
    CHECK(rtw88_async_cancel(a,&t[7])==0);
    CHECK(a->wake_mask==(1U<<1) && a->count[1]==8);
    struct rtw88_async_ticket bad=t[8];bad.serial++;
    CHECK(rtw88_async_cancel(a,&bad)==ESTALE);
    CHECK(a->total==8 && a->count[1]==8);
    for(unsigned i=8;i<16;i++)CHECK(rtw88_async_cancel(a,&t[i])==0);
    CHECK(rtw88_async_cancel(a,&t[15])==ESTALE);
    struct rtw88_async_ticket fresh;
    CHECK(rtw88_async_reserve(a,&initial,2,false,&fresh,&job)==0);
    CHECK(fresh.slot==t[0].slot && fresh.serial!=t[0].serial);
    CHECK(rtw88_async_cancel(a,&t[0])==ESTALE && a->total==1);
    CHECK(rtw88_async_cancel(a,&fresh)==0);
    struct rtw88_tx_job *sentinel=(void *)(uintptr_t)1;
    CHECK(rtw88_async_reserve(a,&initial,RTW88_NTXQ,false,&out,&sentinel)==EINVAL);
    CHECK(out.slot==77 && out.serial==88 && sentinel==(void *)(uintptr_t)1);
    free(a);
}

static void
retry_and_deadline(void)
{
    struct rtw88_async *a=new_queue();
    struct rtw88_async_ticket t,taken;
    struct rtw88_tx_job *job;
    const struct rtw88_tx_job *active;
    uint8_t bytes[64];
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(uint8_t)(0x31+i);
    CHECK(rtw88_async_reserve(a,&initial,1,false,&t,&job)==0);
    memcpy(job->bytes,bytes,sizeof(bytes));
    CHECK(rtw88_async_commit(a,&t,0,0)==EMSGSIZE);
    CHECK(a->total==1);
    CHECK(rtw88_async_commit(a,&t,RTW88_PACKET_SIZE+1U,0)==EMSGSIZE);
    job->operation=1;
    CHECK(rtw88_async_commit(a,&t,sizeof(bytes),0)==EINVAL);
    job->operation=0;job->q=RTW88_NTXQ;
    CHECK(rtw88_async_commit(a,&t,sizeof(bytes),0)==EINVAL);
    job->q=1;
    CHECK(rtw88_async_commit(a,&t,sizeof(bytes),UINT32_MAX-499U)==0);
    CHECK(rtw88_async_cancel(a,&t)==EINVAL);
    CHECK(rtw88_async_take(a,UINT32_MAX-499U,&taken,&active)==0);
    CHECK(taken.serial==t.serial && memcmp(active->bytes,bytes,sizeof(bytes))==0);
    struct rtw88_async_ticket foreign=taken;foreign.serial++;
    CHECK(rtw88_async_finish(a,&foreign,0,RTW88_TX_ACCEPTED,0)==ESTALE);
    CHECK(rtw88_async_finish(a,&taken,0,RTW88_TX_NOT_STARTED,0)==EINVAL);
    CHECK(rtw88_async_cancel(a,&taken)==EINVAL && a->total==1);
    CHECK(rtw88_async_finish(a,&taken,EBUSY,RTW88_TX_NOT_STARTED,0)==0);
    CHECK(a->total==1);
    CHECK(rtw88_async_take(a,499,&taken,&active)==0);
    CHECK(memcmp(active->bytes,bytes,sizeof(bytes))==0);
    CHECK(rtw88_async_finish(a,&taken,EBUSY,RTW88_TX_NOT_STARTED,499)==0);
    CHECK(rtw88_async_take(a,500,&taken,&active)==EAGAIN);
    CHECK(a->total==0);
    CHECK(rtw88_async_reserve(a,&initial,1,false,&t,&job)==0);
    memcpy(job->bytes,bytes,sizeof(bytes));
    CHECK(rtw88_async_commit(a,&t,sizeof(bytes),600)==0);
    CHECK(rtw88_async_take(a,600,&taken,&active)==0);
    CHECK(rtw88_async_finish(a,&taken,EIO,RTW88_TX_MAY_HAVE_WRITTEN,600)==0);
    CHECK(a->total==0 && rtw88_async_take(a,601,&taken,&active)==EAGAIN);
    CHECK(rtw88_async_finish(a,&t,0,RTW88_TX_ACCEPTED,601)==ESTALE);
    free(a);
}

static void
withdraw_and_overflow(void)
{
    struct rtw88_async *a=new_queue();
    struct rtw88_async_ticket reserved,queued,active_ticket;
    struct rtw88_tx_job *producer,*job;
    const struct rtw88_tx_job *active;
    CHECK(rtw88_async_reserve(a,&initial,1,false,&reserved,&producer)==0);
    memset(producer->bytes,0x5a,64);
    CHECK(rtw88_async_reserve(a,&initial,2,false,&queued,&job)==0);
    memset(job->bytes,0x6b,64);
    CHECK(rtw88_async_commit(a,&queued,64,1)==0);
    CHECK(rtw88_async_take(a,1,&active_ticket,&active)==0);
    const struct rtw88_tx_job *held=active;
    CHECK(rtw88_async_reserve(a,&initial,3,false,&queued,&job)==0);
    CHECK(rtw88_async_commit(a,&queued,64,1)==0);
    struct rtw88_async_epoch next=initial;next.key_epoch++;
    rtw88_async_withdraw(a,&next);
    CHECK(a->total==2); /* Reserved and active stay owned; unsent is released. */
    struct rtw88_async_ticket fresh;
    CHECK(rtw88_async_reserve(a,&next,1,false,&fresh,&job)==0);
    CHECK(job!=producer && job!=held);
    memset(job->bytes,0xee,64);
    for(unsigned i=0;i<64;i++)CHECK(held->bytes[i]==0x6b);
    CHECK(rtw88_async_cancel(a,&fresh)==0);
    CHECK(rtw88_async_commit(a,&reserved,64,2)==ESTALE);
    for(unsigned i=0;i<64;i++)CHECK(producer->bytes[i]==0x5a);
    CHECK(held==active && held->id.key_epoch==initial.key_epoch);
    CHECK(rtw88_async_finish(a,&active_ticket,EBUSY,RTW88_TX_NOT_STARTED,2)==0);
    CHECK(a->total==1); /* Stale active is never retried. */
    CHECK(rtw88_async_cancel(a,&reserved)==0);
    CHECK(a->total==0);
    CHECK(rtw88_async_reserve(a,&initial,1,false,&reserved,&job)==ESTALE);
    a->serial=UINT64_MAX;
    CHECK(rtw88_async_reserve(a,&next,1,false,&reserved,&job)==EOVERFLOW);
    CHECK(a->closed && a->error==EOVERFLOW);
    free(a);
    a=new_queue();next=initial;next.epoch=UINT64_MAX;
    rtw88_async_withdraw(a,&next);
    CHECK(a->closed && a->error==EOVERFLOW);
    CHECK(rtw88_async_reserve(a,&next,1,false,&reserved,&job)==EOVERFLOW);
    free(a);
}

static void
closed_admission_drains_owners(void)
{
    struct rtw88_async *a=new_queue();
    struct rtw88_async_ticket old,taken,out={99,99};
    struct rtw88_tx_job *job;
    const struct rtw88_tx_job *active=NULL;
    CHECK(rtw88_async_reserve(a,&initial,1,false,&old,&job)==0);
    memset(job->bytes,0xac,64);
    CHECK(rtw88_async_commit(a,&old,64,1)==0);
    a->serial=UINT64_MAX;
    CHECK(rtw88_async_reserve(a,&initial,1,false,&out,&job)==EOVERFLOW);
    CHECK(out.slot==99 && out.serial==99);
    int error=rtw88_async_take(a,2,&taken,&active);
    CHECK(error==0); /* Closing admission must not orphan a queued owner. */
    if(error==0) {
        CHECK(taken.serial==old.serial && active->bytes[0]==0xac);
        CHECK(rtw88_async_finish(a,&taken,0,RTW88_TX_ACCEPTED,2)==0);
    }
    CHECK(a->total==0);
    CHECK(rtw88_async_take(a,3,&taken,&active)==EOVERFLOW);
    free(a);
}

struct concurrent {
    struct rtw88_async *a;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    unsigned produced,consumed;
    int done;
};
static void
wait_for_ready(struct concurrent *c)
{
    struct timespec until;
    if(clock_gettime(CLOCK_REALTIME,&until)!=0)abort();
    until.tv_sec+=10;
    int error=pthread_cond_timedwait(&c->ready,&c->lock,&until);
    CHECK(error==0);
    if(error!=0)abort();
}
static void *
produce(void *cookie)
{
    struct concurrent *c=cookie;
    for(unsigned i=0;i<20000;i++) {
        pthread_mutex_lock(&c->lock);
        struct rtw88_async_ticket t;
        struct rtw88_tx_job *job;
        int error;
        while((error=rtw88_async_reserve(c->a,&initial,1,false,&t,&job))==ENOBUFS)
            wait_for_ready(c);
        CHECK(error==0);
        memcpy(job->bytes,&i,sizeof(i));
        CHECK(rtw88_async_commit(c->a,&t,sizeof(i),0)==0);
        c->produced++;
        pthread_cond_broadcast(&c->ready);
        pthread_mutex_unlock(&c->lock);
    }
    pthread_mutex_lock(&c->lock);c->done=1;
    pthread_cond_broadcast(&c->ready);pthread_mutex_unlock(&c->lock);
    return NULL;
}
static void *
drain(void *cookie)
{
    struct concurrent *c=cookie;
    pthread_mutex_lock(&c->lock);
    for(;;) {
        struct rtw88_async_ticket t;
        const struct rtw88_tx_job *job;
        int error=rtw88_async_take(c->a,0,&t,&job);
        if(error==EAGAIN) {
            if(c->done)break;
            wait_for_ready(c);continue;
        }
        CHECK(error==0);
        pthread_mutex_unlock(&c->lock); /* Hardware reads immutable active bytes. */
        unsigned value;memcpy(&value,job->bytes,sizeof(value));
        CHECK(value==c->consumed);
        pthread_mutex_lock(&c->lock);
        CHECK(rtw88_async_finish(c->a,&t,0,RTW88_TX_ACCEPTED,0)==0);
        c->consumed++;
        if(c->a->wake_mask) {
            c->a->wake_mask=0;
            pthread_cond_broadcast(&c->ready);
        }
    }
    pthread_mutex_unlock(&c->lock);
    return NULL;
}
static void
threads_do_not_lose_wake(void)
{
    struct concurrent c={.a=new_queue(),.lock=PTHREAD_MUTEX_INITIALIZER,
        .ready=PTHREAD_COND_INITIALIZER};
    pthread_t producer,consumer;
    CHECK(pthread_create(&producer,NULL,produce,&c)==0);
    CHECK(pthread_create(&consumer,NULL,drain,&c)==0);
    CHECK(pthread_join(producer,NULL)==0);
    CHECK(pthread_join(consumer,NULL)==0);
    CHECK(c.produced==20000 && c.consumed==20000 && c.a->total==0);
    pthread_cond_destroy(&c.ready);pthread_mutex_destroy(&c.lock);free(c.a);
}

int
main(void)
{
    bounds_and_control();low_water_and_invalid_owner();retry_and_deadline();
    withdraw_and_overflow();closed_admission_drains_owners();threads_do_not_lose_wake();
    printf("rtw88 async: %u checks, %u failures\n",atomic_load(&checks),atomic_load(&failures));
    return atomic_load(&failures)!=0;
}
