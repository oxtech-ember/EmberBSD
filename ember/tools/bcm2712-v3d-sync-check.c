/* Origin: EmberBSD CM5 binary synchronization acceptance, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_SYNC_CHECK
#include "bcm2712-v3d-csd-check.c"
#include <time.h>

struct sync_wait {
	int fd, result, error;
	struct drm_syncobj_wait args;
	atomic_bool entered;
};

static int64_t
monotonic_ns(void)
{
	struct timespec t;
	if(clock_gettime(CLOCK_MONOTONIC,&t)<0)err(1,"clock_gettime");
	return (int64_t)t.tv_sec*1000000000+t.tv_nsec;
}

static void *
wait_sync(void *arg)
{
	struct sync_wait *w=arg;
	atomic_store(&w->entered,true);
	w->result=ioctl(w->fd,DRM_IOCTL_SYNCOBJ_WAIT,&w->args);
	w->error=errno;
	return NULL;
}

static uint32_t
create_sync(int fd, uint32_t flags)
{
	struct drm_syncobj_create a={.flags=flags};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_CREATE,&a)<0)err(1,"SYNCOBJ_CREATE");
	return a.handle;
}

static void
core_sync(int fd)
{
	struct drm_get_cap cap={.capability=DRM_CAP_SYNCOBJ};
	if(ioctl(fd,DRM_IOCTL_GET_CAP,&cap)<0)err(1,"GET_CAP");
	check(cap.value==1,"binary syncobj capability missing");
	cap.capability=DRM_CAP_SYNCOBJ_TIMELINE;
	if(ioctl(fd,DRM_IOCTL_GET_CAP,&cap)<0)err(1,"timeline GET_CAP");
	check(cap.value==0,"unaccepted timeline capability advertised");
	uint32_t handles[]={create_sync(fd,0),create_sync(fd,0)};
	struct drm_syncobj_array array={.handles=(uintptr_t)handles,.count_handles=2};
	struct drm_syncobj_wait poll={.handles=(uintptr_t)handles,.count_handles=2};
	errno=0;
	check(ioctl(fd,DRM_IOCTL_SYNCOBJ_WAIT,&poll)<0 && errno==EINVAL,"empty input fence");
	for(unsigned round=0;round<16;round++) {
		if(ioctl(fd,DRM_IOCTL_SYNCOBJ_RESET,&array)<0)err(1,"RESET");
		struct sync_wait w={.fd=fd,.args={.handles=(uintptr_t)handles,
		    .count_handles=2,.timeout_nsec=monotonic_ns()+100000000,
		    .flags=DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL|DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT}};
		atomic_init(&w.entered,false);
		pthread_t thread;
		check(pthread_create(&thread,NULL,wait_sync,&w)==0,"create sync waiter");
		while(!atomic_load(&w.entered))sched_yield();
		struct timespec pause={.tv_nsec=1000000};
		check(nanosleep(&pause,NULL)==0,"waiter pause");
		struct drm_syncobj_array signal={.handles=(uintptr_t)handles,
		    .count_handles=round%2?2:1};
		if(ioctl(fd,DRM_IOCTL_SYNCOBJ_SIGNAL,&signal)<0)err(1,"SIGNAL");
		check(pthread_join(thread,NULL)==0,"join sync waiter");
		if(!(round%2?w.result==0:(w.result<0 && w.error==ETIME)))
			errx(1,"WAIT_ALL round %u result=%d errno=%d expected=%d deadline-delta=%lld ns",
			    round,w.result,w.error,ETIME,(long long)(monotonic_ns()-w.args.timeout_nsec));
		check(monotonic_ns()-w.args.timeout_nsec<1000000000,"wait exceeded deadline");
		if(round%2==0)
			check(monotonic_ns()+20000000>=w.args.timeout_nsec,"wait expired before deadline");
	}
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_SIGNAL,&array)<0)err(1,"SIGNAL both");
	struct drm_syncobj_handle exported={.handle=handles[0]};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD,&exported)<0)err(1,"export syncobj");
	struct drm_syncobj_handle imported={.fd=exported.fd};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE,&imported)<0)err(1,"import syncobj");
	check(close(exported.fd)==0,"close syncobj fd");
	for(unsigned i=0;i<2;i++) {
		struct drm_syncobj_destroy destroy={.handle=handles[i]};
		if(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)<0)err(1,"destroy original");
	}
	poll.handles=(uintptr_t)&imported.handle;poll.count_handles=1;
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_WAIT,&poll)<0)err(1,"retained imported syncobj");
	struct drm_syncobj_destroy destroy={.handle=imported.handle};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)<0)err(1,"destroy imported");
	puts("PASS: binary create/reset/signal/wait, finite WAIT_ALL and fd lifetime");
}

static void
alternating(int fd)
{
	struct buffer code=allocate(fd),output=allocate(fd),copy=allocate(fd);
	struct drm_v3d_get_param rev={.param=DRM_V3D_PARAM_V3D_HUB_IDENT3};
	if(ioctl(fd,DRM_IOCTL_V3D_GET_PARAM,&rev)<0)err(1,"HUB_IDENT3");
	uint32_t sync=create_sync(fd,DRM_SYNCOBJ_CREATE_SIGNALED);
	uint32_t handles[]={code.handle,output.handle};
	memcpy(code.map,csd_shader,sizeof(csd_shader));
	struct drm_v3d_submit_csd a={
	    .cfg={0x10000,0x10000,0x10000,0x110,
	        ((rev.value>>8)&255)>=6?1:0,code.gpu|CSD_SHADER_FLAGS,code.gpu+1024},
	    .bo_handles=(uintptr_t)handles,.bo_handle_count=2,.in_sync=sync,.out_sync=sync};
	struct drm_v3d_submit_tfu t={.icfg=0x001d0001,.iia=output.gpu,.ioa=copy.gpu,
	    .iis=64,.ios=0x00400040,.bo_handles={copy.handle,output.handle},
	    .v71={.ioc=0x00400000},.in_sync=sync,.out_sync=sync};
	for(unsigned n=0;n<64;n++) {
		for(unsigned i=0;i<BO_SIZE/4;i++)output.map[i]=copy.map[i]=GUARD;
		code.map[256]=output.gpu;code.map[257]=n*29;
		if(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&a)<0)err(1,"chained CSD");
		if(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_TFU,&t)<0)err(1,"chained TFU");
		struct drm_syncobj_wait w={.handles=(uintptr_t)&sync,.count_handles=1};
		if(ioctl(fd,DRM_IOCTL_SYNCOBJ_WAIT,&w)<0)err(1,"chain fence");
		for(unsigned i=0;i<BO_SIZE/4;i++) {
			uint32_t expected=i<16?n*29+i:GUARD;
			check(output.map[i]==expected && copy.map[i]==expected,"chain data/guard");
		}
	}
	struct drm_syncobj_array reset={.handles=(uintptr_t)&sync,.count_handles=1};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_RESET,&reset)<0)err(1,"reset input");
	errno=0;
	check(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&a)<0 && errno==EINVAL,"empty CSD input");
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_SIGNAL,&reset)<0)err(1,"signal input");
	if(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&a)<0)err(1,"CSD after invalid input");
	struct buffer buffers[]={code,output,copy};
	for(unsigned i=0;i<3;i++) {
		close_handle(fd,buffers[i].handle);
		check(munmap(buffers[i].map,BO_SIZE)==0,"chain munmap");
	}
	struct drm_syncobj_destroy destroy={.handle=sync};
	if(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)<0)err(1,"destroy chain");
	puts("PASS: 64 CSD/TFU fence chains, guards, reset rejection and recovery");
}

int main(int argc,char **argv)
{
	if(argc!=2)errx(1,"usage: %s /dev/dri/renderD128",argv[0]);
	setvbuf(stdout,NULL,_IONBF,0);
	alarm(30);
	int fd=open(argv[1],O_RDWR|O_CLOEXEC);
	if(fd<0)err(1,"open render node");
	core_sync(fd);
	compute_with_sync=true;compute(argv[1],260);
	puts("PASS: 260 same-handle CSD fence replacements, results and full guards");
	alternating(fd);close(fd);
	puts("PASS: native CM5 binary synchronization");
	return 0;
}
