/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Actual probe with bounded syscall models. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef __linux__
typedef int8_t __s8; typedef uint8_t __u8;
typedef int16_t __s16; typedef uint16_t __u16;
typedef int32_t __s32; typedef uint32_t __u32;
typedef int64_t __s64; typedef uint64_t __u64;
typedef size_t __kernel_size_t;
typedef unsigned long drm_handle_t;
#define __user
#endif
#include "pvr_drm.h"
#include "dmabuf-abi.h"
#ifndef F_MAXFD
#define F_MAXFD 10000
#endif

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "CHECK:%d %s\n", __LINE__, #x); abort(); } } while (0)
static unsigned checks;
enum { GOOD, NO_IOCTL, NO_CLOEXEC, POLL_TIMEOUT, POLL_ERROR, BAD_FLAGS,
    BAD_RIGHTS, BAD_IDENTITY, FD_LEAK, FD_QUERY_ERROR, CREATE_FAIL };
enum { NONE, STREAM, DRM, DMABUF, SYNC, LEAK };
static struct { int kind, mode, flags; } descriptors[32];
static int scenario;
static bool bo_live, syncobj_live, leaked;
static unsigned completed_imports, ready_polls;
static int fail(int e) { errno=e; return -1; }
static int allocfd(int kind,int mode,int flags) {
 for(int i=3;i<32;i++)if(!descriptors[i].kind){descriptors[i].kind=kind;descriptors[i].mode=mode;descriptors[i].flags=flags;return i;}
 return fail(EMFILE);
}
static int model_open(const char *path,int flags) {
 CHECK(strcmp(path,"model-render")==0);CHECK(flags==(O_RDWR|O_CLOEXEC));return allocfd(DRM,O_RDWR,FD_CLOEXEC);
}
static int model_close(int fd) {
 CHECK(fd>=3&&fd<32&&descriptors[fd].kind);memset(&descriptors[fd],0,sizeof(descriptors[fd]));return 0;
}
static unsigned model_alarm(unsigned seconds) { CHECK(seconds==120);return 0; }
static int model_fcntl(int fd,int command) {
 if(command==F_MAXFD){for(int i=31;i>=0;i--)if(descriptors[i].kind)return i;return fail(EBADF);}
 if(fd<0||fd>=32||!descriptors[fd].kind)return fail(EBADF);
 if(command==F_GETFL)return descriptors[fd].mode;
 CHECK(command==F_GETFD);
 if(scenario==FD_QUERY_ERROR&&descriptors[fd].kind==SYNC)return fail(EIO);
 return descriptors[fd].flags;
}
static int model_poll(struct pollfd *p,nfds_t count,int milliseconds) {
 CHECK(count==1&&milliseconds==1000&&p->events==POLLIN);
 CHECK(p->fd>=3&&p->fd<32&&descriptors[p->fd].kind==SYNC);
 if(scenario==POLL_TIMEOUT)return 0;
 p->revents=scenario==POLL_ERROR?POLLERR:POLLIN;ready_polls++;return 1;
}
static int model_ioctl(int fd,unsigned long command,void *data) {
 CHECK(fd>=3&&fd<32&&descriptors[fd].kind);
 if(command==DRM_IOCTL_PVR_CREATE_BO){struct drm_pvr_ioctl_create_bo_args *a=data;
  CHECK(descriptors[fd].kind==DRM&&!bo_live&&a->size==4096&&a->flags==DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS);
  if(scenario==CREATE_FAIL)return fail(ENOMEM);bo_live=true;a->handle=1;return 0;}
 if(command==DRM_IOCTL_GEM_CLOSE){struct drm_gem_close *a=data;CHECK(bo_live&&a->handle==1);bo_live=false;return 0;}
 if(command==DRM_IOCTL_PRIME_HANDLE_TO_FD){struct drm_prime_handle *a=data;
  CHECK(bo_live&&a->handle==1);a->fd=allocfd(DMABUF,(a->flags&DRM_RDWR)?O_RDWR:O_RDONLY,FD_CLOEXEC);return a->fd<0?-1:0;}
 if(command==DRM_IOCTL_SYNCOBJ_CREATE){struct drm_syncobj_create *a=data;
  CHECK(!syncobj_live&&a->flags==DRM_SYNCOBJ_CREATE_SIGNALED);syncobj_live=true;a->handle=2;return 0;}
 if(command==DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD){struct drm_syncobj_handle *a=data;
  CHECK(syncobj_live&&a->handle==2&&a->flags==DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE);
  a->fd=allocfd(SYNC,O_RDONLY,0);return a->fd<0?-1:0;}
 if(command==DRM_IOCTL_SYNCOBJ_DESTROY){struct drm_syncobj_destroy *a=data;CHECK(syncobj_live&&a->handle==2);syncobj_live=false;return 0;}
 CHECK(descriptors[fd].kind==DMABUF);
 bool exporting=command==DMA_BUF_IOCTL_EXPORT_SYNC_FILE;
 CHECK(exporting||command==DMA_BUF_IOCTL_IMPORT_SYNC_FILE);
 if(scenario==NO_IOCTL)return fail(ENOTTY);
 uint32_t flags=exporting?((struct dma_buf_export_sync_file *)data)->flags:((struct dma_buf_import_sync_file *)data)->flags;
 if((!flags||(flags&~DMA_BUF_SYNC_RW))&&scenario!=BAD_FLAGS)return fail(EINVAL);
 if((flags&DMA_BUF_SYNC_WRITE)&&descriptors[fd].mode==O_RDONLY&&scenario!=BAD_RIGHTS)return fail(EACCES);
 if(exporting){struct dma_buf_export_sync_file *a=data;
  a->fd=allocfd(SYNC,O_RDONLY,scenario==NO_CLOEXEC?0:FD_CLOEXEC);
  if(scenario==FD_LEAK&&!leaked){CHECK(allocfd(LEAK,O_RDONLY,0)>=0);leaked=true;}
  return a->fd<0?-1:0;}
 struct dma_buf_import_sync_file *a=data;
 if(a->fd<0||a->fd>=32)return fail(EINVAL);
 if(descriptors[a->fd].kind!=SYNC&&scenario!=BAD_IDENTITY)return fail(EINVAL);
 completed_imports++;return 0;
}
#define open model_open
#define close model_close
#define alarm model_alarm
#define fcntl model_fcntl
#define poll model_poll
#define ioctl model_ioctl
#define main probe_main
#include "probe-body.h"
#undef main

int main(void) {
 char *args[]={"sync-check","model-render",NULL};
 for(scenario=GOOD;scenario<=CREATE_FAIL;scenario++){
  memset(descriptors,0,sizeof(descriptors));for(int i=0;i<3;i++)descriptors[i].kind=STREAM;
  bo_live=syncobj_live=leaked=false;completed_imports=ready_polls=0;
  int result=probe_main(2,args);CHECK(result==(scenario==GOOD?0:1));
  CHECK(!bo_live&&!syncobj_live);
  for(int i=3;i<32;i++)CHECK(descriptors[i].kind==NONE||(scenario==FD_LEAK&&descriptors[i].kind==LEAK));
  if(scenario==GOOD){CHECK(completed_imports==4*CYCLES);CHECK(ready_polls==8*CYCLES);}
 }
 printf("DMA-BUF sync probe model: %u checks passed; no kernel or hardware claim\n",checks);
 return 0;
}
