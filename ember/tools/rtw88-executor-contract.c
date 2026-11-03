/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Single-threaded workqueue model; no claim about kernel scheduler races. */
#include "rtw88-executor-fixture.h"
static struct lwp caller={1},worker={2}; struct lwp *curlwp=&caller; int hz=100;
static unsigned calls; static uint32_t seen;
static bool waiting, inject, fail_workqueue; static struct rtw88_softc *injected_sc;
void mutex_init(kmutex_t *m,int a,int b){(void)a;(void)b;*m=0;}
void mutex_enter(kmutex_t *m){assert(*m==0);*m=1;}
void mutex_exit(kmutex_t *m){assert(*m==1);*m=0;}
void mutex_destroy(kmutex_t *m){assert(*m==0);}
void *kmem_zalloc(size_t n,int f){(void)f;void *p=calloc(1,n);if(p==NULL)abort();return p;}
void kmem_free(void *p,size_t n){(void)n;free(p);}
int workqueue_create(struct workqueue **w,const char *n,void (*f)(struct work *,void *),void *a,int p,int i,int flags){(void)n;(void)p;(void)i;(void)flags;if(fail_workqueue)return ENOMEM;*w=calloc(1,sizeof(**w));if(*w==NULL)return ENOMEM;(*w)->func=f;(*w)->arg=a;return 0;}
void workqueue_enqueue(struct workqueue *w,struct work *p,void *a){(void)a;assert(!waiting);assert(!w->pending);w->pending=p;}
void workqueue_wait(struct workqueue *w,struct work *p){(void)p;waiting=true;if(inject){inject=false;rtw88_executor_schedule(injected_sc,RTW88_EXEC_RX);}waiting=false;if(w->pending){struct work *work=w->pending; w->pending=NULL; curlwp=&worker; w->func(work,w->arg); curlwp=&caller;}}
void workqueue_destroy(struct workqueue *w){assert(!w->pending);free(w);}
int kpause(const char *n,bool b,int t,void *m){(void)n;(void)b;(void)t;(void)m;return 0;}
int getticks(void){static int t;return t++;}
static void service(struct rtw88_softc *sc,uint32_t reasons)
{
 assert(rtw88_executor_owned(sc));
 assert(rtw88_executor_drain(sc)==EDEADLK);
 assert(rtw88_executor_destroy(sc)==EDEADLK);
 calls++;seen|=reasons;
 if(calls==1) rtw88_executor_schedule(sc,RTW88_EXEC_TX);
}
int main(void)
{
 struct rtw88_softc sc={0};
 fail_workqueue=true;
 assert(rtw88_executor_create(&sc,service)==ENOMEM && sc.sc_executor==NULL);
 fail_workqueue=false;
 assert(rtw88_executor_create(&sc,service)==0);
 assert(!rtw88_executor_owned(&sc));
 rtw88_executor_schedule(&sc,RTW88_EXEC_CONTROL);
 rtw88_executor_schedule(&sc,RTW88_EXEC_RX);
 assert(rtw88_executor_drain(&sc)==0);
 assert(calls==2 && seen==(RTW88_EXEC_CONTROL|RTW88_EXEC_RX|RTW88_EXEC_TX));
 assert(!rtw88_executor_owned(&sc));
 injected_sc=&sc;inject=true;
 assert(rtw88_executor_drain(&sc)==0);
 assert(!inject && calls==3 && (seen & RTW88_EXEC_RX));
 assert(rtw88_executor_destroy(&sc)==0 && sc.sc_executor==NULL);
 return 0;
}
