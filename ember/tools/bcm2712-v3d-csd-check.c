/* Origin: EmberBSD V3D 7.1 userspace compute acceptance, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse the accepted GEM helpers and run TFU before and after compute. */
#define main tfu_acceptance_main
#include "bcm2712-v3d-render-check.c"
#undef main
#include "bcm2712-v3d-csd-shader.h"

static void
compute(const char *node, unsigned rounds)
{
	int fd=open(node,O_RDWR|O_CLOEXEC), other;
	struct buffer code, output;
	struct drm_v3d_submit_csd a,b;
	struct drm_v3d_get_param rev={.param=DRM_V3D_PARAM_V3D_HUB_IDENT3};
	uint32_t handles[2], saved[BO_SIZE/4];

	if(fd<0)err(1,"open compute node");
	if(ioctl(fd,DRM_IOCTL_V3D_GET_PARAM,&rev)<0)err(1,"HUB_IDENT3");
	code=allocate(fd); output=allocate(fd);
	for(unsigned i=0;i<BO_SIZE/4;i++)code.map[i]=GUARD;
	memcpy(code.map,csd_shader,sizeof(csd_shader));
	handles[0]=code.handle; handles[1]=output.handle;
	a=(struct drm_v3d_submit_csd){
	    .cfg={0x10000,0x10000,0x10000,0x110,
	        ((rev.value>>8)&255)>=6?1:0,code.gpu|CSD_SHADER_FLAGS,code.gpu+1024},
	    .bo_handles=(uintptr_t)handles,.bo_handle_count=2};
	other=open(node,O_RDWR|O_CLOEXEC);
	if(other<0)err(1,"open second compute file");
	errno=0;check(ioctl(other,DRM_IOCTL_V3D_SUBMIT_CSD,&a)<0 && errno==ENOENT,
	    "foreign CSD handles were accepted");
	close(other);
	b=a;b.bo_handles=1;errno=0;
	check(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&b)<0 && errno==EFAULT,"CSD bad pointer");
	/* The end of code can equal the next BO's valid GPU address. */
	b=a;b.cfg[5]=UINT32_MAX & ~7U;errno=0;
	check(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&b)<0 && errno==EINVAL,"CSD code range");
	for(unsigned round=0;round<rounds;round++) {
		unsigned offset=256+(round%32)*128;
		uint32_t seed=0xf0010000U+round*17;
		code.map[256]=output.gpu+offset;code.map[257]=seed;
		memcpy(saved,code.map,sizeof(saved));
		for(unsigned i=0;i<BO_SIZE/4;i++)output.map[i]=GUARD;
		if(ioctl(fd,DRM_IOCTL_V3D_SUBMIT_CSD,&a)<0)err(1,"SUBMIT_CSD round %u",round);
		struct drm_v3d_wait_bo wait={.handle=output.handle};
		if(ioctl(fd,DRM_IOCTL_V3D_WAIT_BO,&wait)<0)err(1,"CSD WAIT_BO");
		for(unsigned i=0;i<BO_SIZE/4;i++) {
			uint32_t expected=i>=offset/4 && i<offset/4+16?seed+i-offset/4:GUARD;
			if(output.map[i]!=expected)errx(1,"round %u word %u: %#x expected %#x",
			    round,i,output.map[i],expected);
			check(code.map[i]==saved[i],"CSD modified code/uniform/guard");
		}
	}
	close_handle(fd,code.handle);close_handle(fd,output.handle);
	check(munmap(code.map,BO_SIZE)==0 && munmap(output.map,BO_SIZE)==0,"CSD munmap");
	close(fd);
}

int
main(int argc,char **argv)
{
	pid_t children[2];
	int status;
	if(argc!=2)errx(1,"usage: %s /dev/dri/renderD128",argv[0]);
	setvbuf(stdout,NULL,_IONBF,0);
	puts("V3D 7.1 compute: 16 integer lane results per dispatch");
	compute(argv[1],260);
	puts("PASS: 260 QPU dispatches, changing uniforms/addresses, counter wrap and full guards");
	for(unsigned i=0;i<2;i++) {
		children[i]=fork();
		if(children[i]<0)err(1,"fork");
		if(children[i]==0) { compute(argv[1],64); _exit(0); }
	}
	for(unsigned i=0;i<2;i++) {
		check(waitpid(children[i],&status,0)==children[i],"waitpid");
		check(WIFEXITED(status) && WEXITSTATUS(status)==0,"parallel CSD client failed");
	}
	puts("PASS: 388 QPU dispatches total; running TFU after CSD cache cleaning");
	if(tfu_acceptance_main(argc,argv)!=0)return 1;
	compute(argv[1],1);
	puts("PASS: 389 QPU dispatches, cross-file denial, CSD/TFU/CSD transitions");
	return 0;
}
