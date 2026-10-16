/* SPDX-License-Identifier: BSD-2-Clause */
/* Concurrent execution of the actual accounting and snapshot functions. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h"
#define BCMV3D_BO_TOTAL (64*1024*1024)
#define BCMV3D_BO_MAX (16*1024*1024)
#define KASSERT assert
struct bcmv3d_device {
	pthread_mutex_t memory_lock;
	size_t allocated;
	struct bcmv3d_bo_stats bo_stats;
};
static void mutex_enter(pthread_mutex_t *m) { assert(pthread_mutex_lock(m)==0); }
static void mutex_exit(pthread_mutex_t *m) { assert(pthread_mutex_unlock(m)==0); }
#include "stats-body.h"
static struct bcmv3d_device device={.memory_lock=PTHREAD_MUTEX_INITIALIZER};
static void *writer(void *unused)
{
	for (unsigned i=0;i<10000;i++)
		bcmv3d_bo_failure(&device,BCMV3D_BO_STAGE_DMA_ALLOC,4097,8192,ENOMEM);
	return NULL;
}
static void *reader(void *unused)
{
	for (unsigned i=0;i<10000;i++) {
		struct bcmv3d_bo_stats s;
		memset(&s,0xa5,sizeof(s));
		bcmv3d_bo_snapshot(&device,&s);
		assert(s.failures==s.failures_by_stage[BCMV3D_BO_STAGE_DMA_ALLOC]);
		assert(s.reserved_bytes==0 && s.highwater_bytes==0);
		assert(s.reserved[0]==0 && s.reserved[1]==0);
		if (s.failures) {
			assert(s.last_requested_bytes==4097 && s.last_rounded_bytes==8192);
			assert(s.last_errno==ENOMEM && s.last_stage==BCMV3D_BO_STAGE_DMA_ALLOC);
		}
	}
	return NULL;
}
int main(void)
{
	pthread_t threads[5];
	for (unsigned i=0;i<5;i++) assert(pthread_create(&threads[i],NULL,i==4 ? reader : writer,NULL)==0);
	for (unsigned i=0;i<5;i++) assert(pthread_join(threads[i],NULL)==0);
	struct bcmv3d_bo_stats s;
	bcmv3d_bo_snapshot(&device,&s);
	assert(s.failures==40000);
	assert(pthread_mutex_destroy(&device.memory_lock)==0);
	puts("PASS concurrent coherent accounting snapshots");
	return 0;
}
