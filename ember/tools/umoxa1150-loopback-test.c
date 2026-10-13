/* Origin: EmberBSD physical UPort 1150 RS-232 loopback probe, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define _POSIX_C_SOURCE 200809L
#define _NETBSD_SOURCE 1
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/usb/umoxa1150io.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void fail(const char *what) { perror(what); exit(1); }
static int64_t now_ms(void) {
 struct timespec t;
 if (clock_gettime(CLOCK_MONOTONIC,&t)) fail("clock_gettime");
 return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void wait_fd(int fd,short events,int64_t until) {
 struct pollfd p={fd,events,0};int r;int64_t left=until-now_ms();
 if(left<=0) { errno=ETIMEDOUT;fail("loopback deadline"); }
 do { r=poll(&p,1,(int)left); } while(r<0 && errno==EINTR);
 if(r==0) { errno=ETIMEDOUT;fail("loopback poll"); }
 if(r<0) fail("poll");
 if(p.revents&(POLLERR|POLLHUP|POLLNVAL)) {errno=EIO;fail("tty poll fault");}
}
static void exchange(int fd,const uint8_t tx[256]) {
 uint8_t rx[256];size_t sent=0,got=0;ssize_t n;int64_t until=now_ms()+3000;
 while(sent<256) {
  n=write(fd,tx+sent,256-sent);
  if(n>0)sent+=(size_t)n;
  else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))wait_fd(fd,POLLOUT,until);
  else if(n<0 && errno==EINTR)continue;
  else fail("write");
 }
 while(got<256) {
  wait_fd(fd,POLLIN,until);n=read(fd,rx+got,256-got);
  if(n>0)got+=(size_t)n;
  else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))continue;
  else {errno=EIO;fail("read");}
 }
 if(memcmp(tx,rx,256)!=0) {
  size_t i;for(i=0;i<256;i++)if(tx[i]!=rx[i])break;
  fprintf(stderr,"FAIL byte %zu: TX=%02x RX=%02x\n",i,tx[i],rx[i]);exit(1);
 }
}
int main(void) {
 const struct {speed_t speed;unsigned baud;} rates[]={{B9600,9600},{B115200,115200}};
 struct termios t;uint8_t tx[256];unsigned r,round,i;int fd,mode=UMOXA1150_MODE_RS232,fault;
 setvbuf(stdout,NULL,_IONBF,0);alarm(30);
 fd=open("/dev/dtyU0",O_RDWR|O_NOCTTY|O_NONBLOCK);if(fd<0)fail("open /dev/dtyU0");
 puts("PASS physical tty open");
 if(ioctl(fd,UMOXA1150_SET_MODE,&mode))fail("set RS232 mode");
 for(r=0;r<2;r++) {
  if(tcgetattr(fd,&t))fail("tcgetattr");
  t.c_iflag=0;t.c_oflag=0;t.c_lflag=0;t.c_cflag=CREAD|CLOCAL|CS8;
  t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
  if(cfsetispeed(&t,rates[r].speed)||cfsetospeed(&t,rates[r].speed)||tcsetattr(fd,TCSANOW,&t))fail("set 8N1 raw speed");
  if(tcflush(fd,TCIOFLUSH))fail("tcflush");
  for(round=0;round<8;round++) {
   for(i=0;i<256;i++)tx[i]=(uint8_t)(i+round*31);
   exchange(fd,tx);
  }
  printf("PASS RS232 %u 8N1, 8 x 256 = 2048 bytes matched\n",rates[r].baud);
 }
 if(ioctl(fd,UMOXA1150_GET_FAULT,&fault))fail("get retained fault");
 if(fault) {errno=fault;fail("retained parent fault");}
 {struct pollfd p={fd,POLLIN,0};if(poll(&p,1,100)>0 && (p.revents&POLLIN)){uint8_t extra;if(read(fd,&extra,1)>0){fprintf(stderr,"FAIL unexpected trailing byte %02x\n",extra);return 1;}}}
 if(close(fd))fail("close");
 puts("PASS 4096 physical loopback bytes, 0 mismatches, retained fault 0");return 0;
}
