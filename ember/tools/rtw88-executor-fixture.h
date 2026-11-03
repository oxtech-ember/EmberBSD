/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#ifndef RTW88_EXECUTOR_FIXTURE_H
#define RTW88_EXECUTOR_FIXTURE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#define RTW88_EXEC_CONTROL 1U
#define RTW88_EXEC_TX 2U
#define RTW88_EXEC_RX 4U
#define RTW88_CTL_DIAG 6
#define MUTEX_DEFAULT 0
#define IPL_NET 0
#define PRI_SOFTNET 0
#define WQ_MPSAFE 0
#define KM_SLEEP 0
#define KERNEL_UNLOCK_ALL(a,b) do {(void)(a);*(b)=0;} while (0)
#define KERNEL_LOCK(n,p) do {(void)(n);(void)(p);} while (0)
typedef int kmutex_t;
struct lwp { int id; };
extern struct lwp *curlwp;
extern int hz;
struct work { int unused; };
struct workqueue { void (*func)(struct work *,void *); void *arg; struct work *pending; };
struct rtw88_executor;
struct rtw88_softc { struct rtw88_executor *sc_executor; };
struct rtw88_async_epoch { uint32_t generation; uint64_t epoch,key_epoch; };
struct rtw88_control_snapshot { int kind; struct rtw88_async_epoch id; };
struct rtw88_control_result { struct rtw88_async_epoch id; int error; };
typedef void (*rtw88_executor_service_fn)(struct rtw88_softc *,uint32_t);
void mutex_init(kmutex_t *,int,int); void mutex_enter(kmutex_t *); void mutex_exit(kmutex_t *); void mutex_destroy(kmutex_t *);
void *kmem_zalloc(size_t,int); void kmem_free(void *,size_t);
int workqueue_create(struct workqueue **,const char *,void (*)(struct work *,void *),void *,int,int,int);
void workqueue_enqueue(struct workqueue *,struct work *,void *); void workqueue_wait(struct workqueue *,struct work *); void workqueue_destroy(struct workqueue *);
int kpause(const char *,bool,int,void *); int getticks(void);
int rtw88_executor_create(struct rtw88_softc *,rtw88_executor_service_fn);
void rtw88_executor_schedule(struct rtw88_softc *,uint32_t);
int rtw88_executor_owned(struct rtw88_softc *);
int rtw88_executor_drain(struct rtw88_softc *); int rtw88_executor_destroy(struct rtw88_softc *);
#endif

#define KASSERT assert
