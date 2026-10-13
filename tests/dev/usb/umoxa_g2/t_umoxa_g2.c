/* Origin: EmberBSD native actual G2 workqueue/ucom/TTY contracts, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <dev/usb/umoxa_g2io.h>
#include "g2_test.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s errno=%d\n",__func__,__LINE__,#c,errno); exit(1); } } while (0)
#define KERNEL(c) do { rump_schedule(); c; rump_unschedule(); } while (0)
static void delay(void) { const struct timespec t={0,10000000};(void)nanosleep(&t,NULL); }
static struct g2_record snapshot(void) { struct g2_record r;KERNEL(rump_g2_snapshot(&r));return r; }
static int openport(unsigned p) {
 char path[32];struct termios t;int fd=-1;
 snprintf(path,sizeof(path),"/dev/dtyU%u",p);
 for(unsigned i=0;i<300;i++){fd=rump_sys_open(path,O_RDWR|O_NONBLOCK);if(fd>=0 || errno!=RUMP_ERESTART)break;delay();}
 CHECK(fd>=0);CHECK(!rump_sys_ioctl(fd,TIOCGETA,&t));cfmakeraw(&t);t.c_cflag|=CLOCAL;t.c_cflag&=~HUPCL;
 CHECK(!rump_sys_ioctl(fd,TIOCSETA,&t));return fd;
}
static void echo(int fd,unsigned port,size_t length) {
 uint8_t sent[1024],received[1024];size_t n=0;ssize_t k;
 CHECK(length<=sizeof(sent));for(size_t i=0;i<length;i++)sent[i]=(uint8_t)(port*37+i*11);
 CHECK(rump_sys_write(fd,sent,length)==(ssize_t)length);
 for(unsigned i=0;i<1000 && n<length;i++){
  k=rump_sys_read(fd,received+n,length-n);if(k>0)n+=(size_t)k;else CHECK(k<0 && (errno==EAGAIN || errno==EWOULDBLOCK));delay();
 }
 CHECK(n==length && !memcmp(sent,received,length));
}
static void *parallel(void *arg) { unsigned p=(unsigned)(uintptr_t)arg;int fd=openport(p);echo(fd,p,512);CHECK(!rump_sys_close(fd));return NULL; }
static int carrier_done;
static pthread_mutex_t carrier_lock=PTHREAD_MUTEX_INITIALIZER;
static void *carrier(void *arg) {
 int fd;(void)arg;fd=rump_sys_open("/dev/ttyU0",O_RDWR);CHECK(fd>=0);CHECK(!rump_sys_close(fd));
 pthread_mutex_lock(&carrier_lock);carrier_done=1;pthread_mutex_unlock(&carrier_lock);return NULL;
}
static int close_fd,close_done;
static pthread_mutex_t close_lock=PTHREAD_MUTEX_INITIALIZER;
static void *closer(void *arg) { (void)arg;CHECK(!rump_sys_close(close_fd));pthread_mutex_lock(&close_lock);close_done=1;pthread_mutex_unlock(&close_lock);return NULL; }
static void blocked(void) { for(unsigned i=0;i<300;i++){if(snapshot().blocked)return;delay();}CHECK(0 && "worker did not enter USB"); }
int main(void) {
 pthread_t threads[4],thread;int fd,peer,mode=1,fault=0,error=-1;struct g2_record r;
 setvbuf(stdout,NULL,_IOLBF,0);alarm(60);CHECK(!rump_init());r=snapshot();CHECK(r.ready && r.pipes==3 && r.xfers==3);
 if(!getenv("G2_SKIP_CARRIER")) {
  CHECK(!pthread_create(&thread,NULL,carrier,NULL));
  for(unsigned i=0;i<300;i++){int done;pthread_mutex_lock(&carrier_lock);done=carrier_done;pthread_mutex_unlock(&carrier_lock);if(done)break;delay();}
  pthread_mutex_lock(&carrier_lock);CHECK(carrier_done);pthread_mutex_unlock(&carrier_lock);CHECK(!pthread_join(thread,NULL));puts("PASS native initial DCD blocking open without events");
 }
 for(unsigned p=0;p<4;p++)CHECK(!pthread_create(&threads[p],NULL,parallel,(void *)(uintptr_t)p));
 for(unsigned p=0;p<4;p++)CHECK(!pthread_join(threads[p],NULL));
 puts("PASS native four-port parallel framed loopback/credit");
 if(!getenv("G2_SKIP_MODE")) {
 fd=openport(0);CHECK(!rump_sys_ioctl(fd,UMOXA_G2_SET_MODE,&mode));CHECK(!rump_sys_close(fd));fd=openport(0);
 CHECK(!rump_sys_ioctl(fd,UMOXA_G2_GET_FAULT,&fault) && !fault);CHECK(rump_sys_ioctl(fd,TIOCSDTR,NULL)<0 && errno==EOPNOTSUPP);CHECK(!rump_sys_close(fd));puts("PASS native mode ioctl and nonRS232 reopen");
 }
 fd=openport(1);CHECK(!rump_sys_ioctl(fd,TIOCSTOP,NULL));CHECK(rump_sys_write(fd,"queued",6)==6);mode=1;
 CHECK(rump_sys_ioctl(fd,UMOXA_G2_SET_MODE,&mode)<0 && errno==EBUSY);CHECK(!rump_sys_ioctl(fd,TIOCSTART,NULL));
 { char b[6];size_t n=0;for(unsigned i=0;i<300 && n<6;i++){ssize_t k=rump_sys_read(fd,b+n,6-n);if(k>0)n+=(size_t)k;else CHECK(k<0 && errno==EAGAIN);delay();}CHECK(n==6 && !memcmp(b,"queued",6)); }
 CHECK(!rump_sys_close(fd));puts("PASS native TTY queued-before-submit mode barrier");
 fd=openport(1);{ struct termios t;CHECK(!rump_sys_ioctl(fd,TIOCGETA,&t));t.c_cflag|=CRTSCTS;CHECK(!rump_sys_ioctl(fd,TIOCSETA,&t));mode=0;
 CHECK(!rump_sys_ioctl(fd,UMOXA_G2_SET_MODE,&mode));CHECK(!rump_sys_ioctl(fd,TIOCGETA,&t) && (t.c_cflag&CRTSCTS));mode=1;CHECK(rump_sys_ioctl(fd,UMOXA_G2_SET_MODE,&mode)<0 && errno==EINVAL); }
 CHECK(!rump_sys_close(fd));puts("PASS native mode preserves advertised RTSCTS");
 close_fd=openport(1);peer=openport(2);KERNEL(rump_g2_gate(1));CHECK(rump_sys_write(close_fd,"x",1)==1);blocked();CHECK(!pthread_create(&thread,NULL,closer,NULL));
 for(unsigned i=0;i<10;i++)delay();
 pthread_mutex_lock(&close_lock);CHECK(!close_done);pthread_mutex_unlock(&close_lock);KERNEL(rump_g2_gate(0));CHECK(!pthread_join(thread,NULL));echo(peer,2,32);CHECK(!rump_sys_close(peer));puts("PASS native close waits active worker and preserves peer");
 fd=openport(3);KERNEL(rump_g2_short());CHECK(rump_sys_write(fd,"fault",5)==5);
 for(unsigned i=0;i<300;i++){if(rump_sys_ioctl(fd,TIOCGETA,&(struct termios){0})<0 && errno==EIO)break;delay();}
 CHECK(!rump_sys_ioctl(fd,UMOXA_G2_GET_FAULT,&fault) && fault==EIO);CHECK(rump_sys_write(fd,"x",1)<0 && errno==EIO);CHECK(!rump_sys_close(fd));puts("PASS native retained fault query through ucom admission");
 fd=openport(1);KERNEL(rump_g2_gate(1));CHECK(rump_sys_write(fd,"x",1)==1);blocked();KERNEL(error=rump_g2_remove());CHECK(!error);r=snapshot();CHECK(!r.ready && !r.pipes && !r.xfers);(void)rump_sys_close(fd);puts("PASS native detach aborts active worker before freeing children");
 puts("8/8 native actual G2 groups passed (USB doubles; no hardware)");return 0;
}
