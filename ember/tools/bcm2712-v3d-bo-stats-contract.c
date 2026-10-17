/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the actual allocator, snapshot, and sysctl adapter with service doubles. */
#define BCMV3D_JOB_CONTRACT
#include "bcm2712-v3d-bo-contract.c"

struct sysctlnode { void *sysctl_data; size_t sysctl_size; };
struct sysctllog { unsigned nodes; };
struct lwp;
#define SYSCTLFN_ARGS const int *name, u_int namelen, void *oldp, \
    size_t *oldlenp, const void *newp, size_t newlen, const int *oname, \
    struct lwp *l, const struct sysctlnode *rnode
#define SYSCTLFN_CALL(node) name, namelen, oldp, oldlenp, newp, newlen, oname, l, node
#define CTLFLAG_READONLY 0
#define CTLTYPE_NODE 1
#define CTLTYPE_STRUCT 2
#define CTL_HW 6
#define CTL_CREATE -1
#define CTL_EOL -2
#define SYSCTL_DESCR(s) (s)
static struct bcmv3d_device *queried;
static struct sysctllog test_log;
static struct sysctlnode test_node;
static unsigned sysctl_calls, sysctl_fail, teardown_calls, error_logs;
static bool published;
static int copy_error;
static const char *device_xname(device_t d) { return "v3d-test"; }
static void aprint_error_dev(device_t d, const char *fmt, int e)
{ CHECK(e != 0); error_logs++; }
static void sysctl_teardown(struct sysctllog **log)
{ teardown_calls++; if (*log) (*log)->nodes=0; *log=NULL; published=false; }
static int sysctl_createv(struct sysctllog **log, int cflags,
    const struct sysctlnode **parent, const struct sysctlnode **output,
    int flags, int type, const char *name, const char *description,
    int (*handler)(SYSCTLFN_ARGS), uint64_t immediate, void *data, size_t size, ...)
{
	CHECK(flags == CTLFLAG_READONLY && cflags == 0);
	sysctl_calls++;
	if (sysctl_calls == sysctl_fail) return ENOMEM;
	*log=&test_log; test_log.nodes++;
	if (type == CTLTYPE_NODE) {
		CHECK(parent == NULL && output != NULL && handler == NULL);
		*output=&test_node;
	} else {
		CHECK(type == CTLTYPE_STRUCT && parent && *parent == &test_node);
		CHECK(strcmp(name,"bo_stats") == 0 && handler != NULL);
		CHECK(data == queried && size == sizeof(struct bcmv3d_bo_stats));
		published=true;
	}
	return 0;
}
static int sysctl_lookup(SYSCTLFN_ARGS)
{
	CHECK(queried->memory_lock == 0); /* No user copy under the allocator lock. */
	CHECK(newp == NULL && rnode->sysctl_data != queried);
	if (copy_error) return copy_error;
	if (namelen) return ENOTDIR;
	if (!oldp) { *oldlenp=rnode->sysctl_size; return 0; }
	if (*oldlenp < rnode->sysctl_size) return ENOMEM;
	memcpy(oldp,rnode->sysctl_data,rnode->sysctl_size);
	*oldlenp=rnode->sysctl_size;
	return 0;
}
#include "sysctl-body.h"

static struct bcmv3d_bo_stats
snapshot(struct bcmv3d_device *c)
{
	struct bcmv3d_bo_stats s;
	memset(&s,0xa5,sizeof(s));
	unsigned lock_checks=checks;
	bcmv3d_bo_snapshot(c,&s);
	CHECK(checks==lock_checks+3);
	CHECK(s.version==BCMV3D_BO_STATS_VERSION && s.length==sizeof(s));
	CHECK(s.reserved_bytes==c->allocated && s.highwater_bytes>=c->allocated);
	CHECK(s.limit_bytes==BCMV3D_BO_TOTAL && s.max_bo_bytes==BCMV3D_BO_MAX);
	CHECK(s.reserved[0]==0 && s.reserved[1]==0);
	return s;
}

int
main(void)
{
	struct bcmv3d_device c={.bo_total=BCMV3D_BO_TOTAL};
	struct drm_device d={.dev_private=&c};
	struct drm_file f={0}, other={0};
	struct drm_v3d_create_bo a;
	struct bcmv3d_bo_stats s;
	uint64_t before;
	c.drm=&d;
	queried=&c;
	CHECK(sizeof(s)==192);
	CHECK(snapshot(&c).failures==0);
	const enum bcmv3d_bo_failure_stage stages[]={0,
	    BCMV3D_BO_STAGE_VA, BCMV3D_BO_STAGE_DMA_ALLOC, BCMV3D_BO_STAGE_DMA_MAP,
	    BCMV3D_BO_STAGE_MAP_CREATE, BCMV3D_BO_STAGE_MAP_LOAD, BCMV3D_BO_STAGE_HANDLE};
	for (fail=1;fail<=6;fail++) {
		before=snapshot(&c).failures;
		a=(struct drm_v3d_create_bo){.size=4097};
		CHECK(bcmv3d_create_bo(&d,&a,&f)<0);
		s=snapshot(&c);
		CHECK(s.failures==before+1 && s.failures_by_stage[stages[fail]]==1);
		CHECK(s.last_stage==(uint32_t)stages[fail]);
		CHECK(s.last_errno==(fail==5 ? EIO : ENOMEM));
		CHECK(s.last_requested_bytes==4097 && s.last_rounded_bytes==8192);
		CHECK(s.last_reserved_bytes==(fail==1 ? 0 : 8192));
		CHECK(s.reservations==s.releases && s.reserved_bytes==0);
		empty(&c);
	}
	fail=0;
	for (bad_geometry=1;bad_geometry<=10;bad_geometry++) {
		before=snapshot(&c).failures;
		a=(struct drm_v3d_create_bo){.size=8192};
		CHECK(bcmv3d_create_bo(&d,&a,&f)==-EINVAL);
		s=snapshot(&c);
		CHECK(s.failures==before+1 && s.last_stage==BCMV3D_BO_STAGE_GEOMETRY);
		CHECK(s.last_errno==EINVAL && s.last_reserved_bytes==8192);
		empty(&c);
	}
	bad_geometry=0;
	const uint32_t invalid[]={0,BCMV3D_BO_MAX+1,UINT32_MAX};
	for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
		before=snapshot(&c).failures;
		a=(struct drm_v3d_create_bo){.size=invalid[i]};
		CHECK(bcmv3d_create_bo(&d,&a,&f)==-EINVAL);
		s=snapshot(&c);
		CHECK(s.failures==before+1 && s.last_stage==BCMV3D_BO_STAGE_REQUEST);
		CHECK(s.last_requested_bytes==invalid[i] && s.last_rounded_bytes==0);
		empty(&c);
	}
	a=(struct drm_v3d_create_bo){.size=4096,.flags=1};
	before=snapshot(&c).failures;
	CHECK(bcmv3d_create_bo(&d,&a,&f)==-EINVAL);
	CHECK(snapshot(&c).failures==before+1);

	/* Internal owners and two files share one device budget. */
	struct bcmv3d_bo *internal=bcmv3d_bo_alloc(&c,4*1024*1024+3*PAGE_SIZE);
	CHECK(!IS_ERR(internal));
	uint32_t handles[4];
	for (unsigned i=0;i<4;i++) {
		a=(struct drm_v3d_create_bo){.size=i<3 ? BCMV3D_BO_MAX : 12*1024*1024-3*PAGE_SIZE};
		CHECK(bcmv3d_create_bo(&d,&a,i&1 ? &other : &f)==0);
		handles[i]=a.handle;
	}
	s=snapshot(&c);
	CHECK(s.reserved_bytes==BCMV3D_BO_TOTAL && s.highwater_bytes==BCMV3D_BO_TOTAL);
	before=s.failures;
	a=(struct drm_v3d_create_bo){.size=PAGE_SIZE};
	CHECK(bcmv3d_create_bo(&d,&a,&f)==-ENOMEM);
	s=snapshot(&c);
	CHECK(s.failures==before+1 && s.last_stage==BCMV3D_BO_STAGE_QUOTA);
	CHECK(s.last_reserved_bytes==BCMV3D_BO_TOTAL && s.last_errno==ENOMEM);
	/* Model the extra reference held by a mapping or PRIME export. */
	struct drm_gem_object *held=f.handles[handles[0]];
	drm_gem_object_get(held);
	close_handle(&f,handles[0]);
	CHECK(snapshot(&c).reserved_bytes==BCMV3D_BO_TOTAL);
	drm_gem_object_put_unlocked(held);
	CHECK(snapshot(&c).reserved_bytes==BCMV3D_BO_TOTAL-BCMV3D_BO_MAX);
	for (unsigned i=1;i<4;i++) close_handle(i&1 ? &other : &f,handles[i]);
	drm_gem_object_put_unlocked(&internal->cma.base);
	empty(&c);
	s=snapshot(&c);
	CHECK(s.highwater_bytes==BCMV3D_BO_TOTAL && s.reservations==s.releases);

	/* Read-only, complete snapshots; copyout failure does not change state. */
	struct sysctlnode node={.sysctl_data=&c};
	size_t length=sizeof(s);
	CHECK(bcmv3d_bo_sysctl(NULL,0,&s,&length,NULL,0,NULL,NULL,&node)==0);
	CHECK(length==sizeof(s) && s.reserved_bytes==0);
	struct bcmv3d_bo_stats saved=s;
	CHECK(bcmv3d_bo_sysctl(NULL,0,&s,&length,&s,sizeof(s),NULL,NULL,&node)==EPERM);
	length=sizeof(s)-1;
	CHECK(bcmv3d_bo_sysctl(NULL,0,&s,&length,NULL,0,NULL,NULL,&node)==ENOMEM);
	length=0;
	CHECK(bcmv3d_bo_sysctl(NULL,0,NULL,&length,NULL,0,NULL,NULL,&node)==0 && length==sizeof(s));
	copy_error=EFAULT;
	CHECK(bcmv3d_bo_sysctl(NULL,0,&s,&length,NULL,0,NULL,NULL,&node)==EFAULT);
	copy_error=0;
	s=snapshot(&c);
	CHECK(memcmp(&s,&saved,sizeof(s))==0);

	/* Optional registration failures remove all nodes before sc can be freed. */
	for (sysctl_fail=1;sysctl_fail<=2;sysctl_fail++) {
		sysctl_calls=teardown_calls=error_logs=0;
		bcmv3d_bo_sysctl_init(&c);
		CHECK(!published && !c.sysctllog && test_log.nodes==0);
		CHECK(teardown_calls==1 && error_logs==1);
	}
	sysctl_fail=sysctl_calls=teardown_calls=error_logs=0;
	bcmv3d_bo_sysctl_init(&c);
	CHECK(published && c.sysctllog && test_log.nodes==2 && !error_logs);
	sysctl_teardown(&c.sysctllog);
	CHECK(!published && !c.sysctllog && !test_log.nodes);

	/* Lifetime counters saturate instead of wrapping into false small values. */
	c.bo_stats.failures=c.bo_stats.failures_by_stage[BCMV3D_BO_STAGE_REQUEST]=UINT64_MAX;
	bcmv3d_bo_failure(&c,BCMV3D_BO_STAGE_REQUEST,0,0,EINVAL);
	s=snapshot(&c);
	CHECK(s.failures==UINT64_MAX && s.failures_by_stage[BCMV3D_BO_STAGE_REQUEST]==UINT64_MAX);
	puts("PASS native BO accounting, allocator unwind, ownership, sysctl and teardown contract");
	return 0;
}
