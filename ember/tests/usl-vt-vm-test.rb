# SPDX-License-Identifier: BSD-2-Clause
# Compile portable parts of the real VM runner. Never open a wscons device.
require 'open3'
require 'shellwords'
require 'fileutils'
require 'digest'
root = File.expand_path(ARGV.fetch(0))
work = ENV.fetch('USL_VM_WORK') { abort 'Set USL_VM_WORK to a private receipt directory' }
FileUtils.mkdir_p(work)
source = ENV.fetch('USL_VM_SOURCE', root + '/ember/tests/usl-vt-vm.c')
if (mutation = ENV['USL_VM_MUTATION'])
  text = File.read(source)
  changes = {
    'no-vm-guard' => ['return (!explicit_vm ||', 'return (0 ||'],
    'loose-fixture-mode' => ['(st->st_mode & 07777) == 0600', '1'],
    'no-wait-deadline' => ['if (now_ms() >= deadline)', 'if (now_ms() >= deadline && 0)'],
    'no-child-reap' => ['if (pid <= 0)', 'if (pid <= 0 || 1)'],
    'ignore-reset-failure' => ['if (current == 1 && ioctl(fd, VT_SETMODE, &automatic))', 'if (current == 1 && ioctl(fd, VT_SETMODE, &automatic) && 0)'],
    'unknown-vt-activation' => ['active != vt || ioctl', '((void)vt, 0) || ioctl']
  }
  from, to = changes.fetch(mutation) { abort 'Unknown mutation' }
  abort 'Mutation does not match once' unless text.scan(from).size == 1
  text.sub!(from, to)
  source = work + '/mutated.c'
  File.write(source, text)
end
harness = <<~'C'
#define USL_VM_HOST_TEST 1
#include "@SOURCE@"
/* Always evaluate checks, including in an NDEBUG build. */
#define EXPECT(expr) do { if (!(expr)) { fprintf(stderr, "Assertion failed: (%s), line %d\n", #expr, __LINE__); abort(); } } while (0)

#include <stdarg.h>
#include <sys/ioctl.h>
typedef unsigned char unchar;
typedef unsigned short ushort;
#include "@HEADER@"
@DISPLAY_MACROS@
static int fake_mode, fake_back, fake_active, reset_calls, activate_calls;
static int fail_reset, fail_activate, fail_await, wrong_text;
static int own_mode(int fd) { return fd==10 ? fake_mode : fake_back; }
static int get_active(int fd) { EXPECT(fd==12); return fake_active; }
static int await_active(int fd, int vt) { EXPECT(fd==12 && vt==1); return fail_await ? -1 : 0; }
static int test_ioctl(int fd, unsigned long op, ...) {
 va_list a; va_start(a,op); int r=0;
 if(op==VT_SETMODE) { struct vt_mode *m=va_arg(a,struct vt_mode *); EXPECT(fd==10 && m->mode==VT_AUTO); reset_calls++; if(fail_reset) r=-1; else fake_mode=0; }
 else if(op==VT_ACTIVATE) { int vt=va_arg(a,int); EXPECT(fd==12 && vt==1); activate_calls++; if(fail_activate)r=-1;else fake_active=vt; }
 else if(op==WSDISPLAYIO_GMODE) { unsigned int *m=va_arg(a,unsigned int *); EXPECT(fd==10 || fd==11); *m=wrong_text ? 1 : WSDISPLAYIO_MODE_EMUL; }
 else abort();
 va_end(a); return r;
}
#define ioctl test_ioctl
@RESTORE@
#undef ioctl
static void clean_fake(void) {fake_mode=1;fake_back=0;fake_active=5;reset_calls=activate_calls=fail_reset=fail_activate=fail_await=wrong_text=0;}

static int count_fds(void) { int n=0; for(int fd=0;fd<256;fd++) if(fcntl(fd,F_GETFD)!=-1) n++; return n; }
int main(void) {
 { struct options o; char *args[]={"runner","--vm-fixture","--fixture-record","/fixture","--screen","/dev/ttyE4","--return-screen","/dev/ttyE0","--uid","1000"};
   EXPECT(parse_options(10,args,&o)==0 && o.vt==5 && o.original==1 && o.uid==1000);
   EXPECT(parse_options(9,args+1,&o)==-1);
   args[1]="--not-a-guard"; EXPECT(parse_options(10,args,&o)==-1); args[1]="--vm-fixture";
   args[9]="0"; EXPECT(parse_options(10,args,&o)==-1);
   args[9]="-1"; EXPECT(parse_options(10,args,&o)==-1);
   args[9]="1000"; args[7]="/dev/ttyE4"; EXPECT(parse_options(10,args,&o)==-1);
   args[7]="/dev/ttyE0"; args[3]="relative"; EXPECT(parse_options(10,args,&o)==-1);
   args[3]="/fixture"; EXPECT(parse_options(9,args,&o)==-1);
 }


clean_fake(); EXPECT(restore_fixture(12,10,11,5,1)==0 && reset_calls==1 && activate_calls==1);
clean_fake(); fake_mode=0;fake_active=1; EXPECT(restore_fixture(12,10,11,5,1)==0 && !reset_calls && !activate_calls);
clean_fake(); fake_mode=-1; EXPECT(restore_fixture(12,10,11,5,1)==-1 && !reset_calls && !activate_calls);
clean_fake(); fake_back=1; EXPECT(restore_fixture(12,10,11,5,1)==-1 && !reset_calls && !activate_calls);
clean_fake(); fail_reset=1; EXPECT(restore_fixture(12,10,11,5,1)==-1 && !activate_calls);
clean_fake(); fake_active=9; EXPECT(restore_fixture(12,10,11,5,1)==-1 && !activate_calls);
clean_fake(); fail_activate=1; EXPECT(restore_fixture(12,10,11,5,1)==-1);
clean_fake(); fail_await=1; EXPECT(restore_fixture(12,10,11,5,1)==-1);
clean_fake(); wrong_text=1; EXPECT(restore_fixture(12,10,11,5,1)==-1);
EXPECT(set_signal(-1,SIG_DFL)==-1);
 unsigned long n;
 int vt, status, fds[2], fd, got; struct stat st; pid_t child;
 EXPECT(decimal("1", 100, &n)==0 && n==1);
 EXPECT(decimal("0", 100, &n)==0 && n==0);
 EXPECT(decimal("01", 100, &n)==-1);
 EXPECT(decimal("", 100, &n)==-1);
 EXPECT(decimal("-1", 100, &n)==-1);
 EXPECT(decimal("+1", 100, &n)==-1);
 EXPECT(decimal("101", 100, &n)==-1);
 EXPECT(decimal("9999999999999999999999999999999", 100, &n)==-1);
 EXPECT(screen_number("/dev/ttyE0", &vt)==0 && vt==1);
 EXPECT(screen_number("/dev/ttyE14", &vt)==0 && vt==15);
 EXPECT(screen_number("/dev/ttyE15", &vt)==-1);
 EXPECT(screen_number("/dev/ttyE04", &vt)==-1);
 EXPECT(screen_number("/dev/../ttyE4", &vt)==-1);
 EXPECT(screen_number("/dev/ttyE4/a", &vt)==-1);
 memset(&st,0,sizeof(st)); st.st_mode=S_IFREG|0600; st.st_uid=0; st.st_nlink=1;
 EXPECT(fixture_stat(&st)==0);
 st.st_mode=S_IFREG|0640; EXPECT(fixture_stat(&st)==-1);
 st.st_mode=S_IFREG|0600; st.st_uid=1; EXPECT(fixture_stat(&st)==-1);
 st.st_uid=0; st.st_mode=S_IFCHR|0600; EXPECT(fixture_stat(&st)==-1);
 st.st_mode=S_IFREG|0600; st.st_nlink=2; EXPECT(fixture_stat(&st)==-1);
 EXPECT(socketpair(AF_UNIX,SOCK_STREAM,0,fds)==0);
 fd=open("/dev/null",O_RDONLY); EXPECT(fd>=0);
 EXPECT(pass_fd(fds[0],fd)==0); got=take_fd(fds[1]); EXPECT(got>=0);
 EXPECT(fstat(got,&st)==0 && S_ISCHR(st.st_mode)); close(got); close(fd);
 EXPECT(write(fds[0],"X",1)==1); EXPECT(take_fd(fds[1])==-1);
 { int before=count_fds(); int pair[2]; union {struct cmsghdr c; char b[CMSG_SPACE(sizeof(pair))];} u;
  struct msghdr m; char b='F'; struct iovec v={&b,1}; struct cmsghdr *c;
  memset(&m,0,sizeof(m)); memset(&u,0,sizeof(u));
  pair[0]=open("/dev/null",O_RDONLY); pair[1]=dup(pair[0]); EXPECT(pair[0]>=0 && pair[1]>=0);
  m.msg_iov=&v; m.msg_iovlen=1; m.msg_control=u.b; m.msg_controllen=sizeof(u.b);
  c=CMSG_FIRSTHDR(&m); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS; c->cmsg_len=CMSG_LEN(sizeof(pair));
  memcpy(CMSG_DATA(c),pair,sizeof(pair)); EXPECT(sendmsg(fds[0],&m,0)==1);
  close(pair[0]); close(pair[1]); EXPECT(take_fd(fds[1])==-1); EXPECT(count_fds()==before);
}
close(fds[0]); close(fds[1]);

 child=fork(); EXPECT(child>=0); if(child==0) _exit(7);
 EXPECT(reap(child,1000,&status)==0 && WIFEXITED(status) && WEXITSTATUS(status)==7);
 EXPECT(waitpid(child,&status,WNOHANG)==-1 && errno==ECHILD);
 child=fork(); EXPECT(child>=0); if(child==0) { struct timespec t={0,400000000}; nanosleep(&t,NULL); _exit(0); }
 { int64_t start=now_ms(); EXPECT(reap(child,20,&status)==-1 && errno==ETIMEDOUT); EXPECT(now_ms()-start<200); }
 EXPECT(stop_child(child)==0); EXPECT(waitpid(child,&status,WNOHANG)==-1 && errno==ECHILD);
EXPECT(socketpair(AF_UNIX,SOCK_STREAM,0,fds)==0);
child=fork(); EXPECT(child>=0);
if(child==0) { struct timespec t={1,0}; close(fds[0]); EXPECT(set_signal(SIGTERM,SIG_IGN)==0); EXPECT(write(fds[1],"R",1)==1); nanosleep(&t,NULL); _exit(0); }
close(fds[1]); { char byte; EXPECT(ready(fds[0],POLLIN,now_ms()+1000)==0); EXPECT(read(fds[0],&byte,1)==1 && byte=='R'); } close(fds[0]);
{ int64_t start=now_ms(); EXPECT(stop_child(child)==0); EXPECT(now_ms()-start>=180 && now_ms()-start<1500); }
EXPECT(waitpid(child,&status,WNOHANG)==-1 && errno==ECHILD);
EXPECT(socketpair(AF_UNIX,SOCK_STREAM,0,fds)==0); fd=open("/dev/null",O_RDONLY); EXPECT(fd>=0);
child=fork(); EXPECT(child>=0);
if(child==0) {close(fds[0]);close(fd);close(0);got=take_fd(fds[1]);EXPECT(got==0);EXPECT(fstat(got,&st)==0 && S_ISCHR(st.st_mode));close(got);_exit(0);}
close(fds[1]); EXPECT(pass_fd(fds[0],fd)==0); close(fd);close(fds[0]);
EXPECT(reap(child,1000,&status)==0 && WIFEXITED(status) && WEXITSTATUS(status)==0);
 puts("PASS parser/fixture guards, actual SCM_RIGHTS rejection/closure, deadline/child reaping and nine real restoration paths");
 return 0;
}
C
text = File.read(source)
a = text.index("static int\nrestore_fixture(") or abort 'Missing restore fixture function'
b = text.index("static int\nscreen_fd(", a) or abort 'Missing restore fixture end'
header = root + '/sys/dev/wscons/wsdisplay_usl_io.h'
wscons = File.read(root + '/sys/dev/wscons/wsconsio.h')
macros = %w[WSDISPLAYIO_GMODE WSDISPLAYIO_MODE_EMUL].map { |name| wscons.lines.find { |l| l.match?(/^#define\s+#{name}\s/) } || abort("Missing #{name}") }.join
harness = harness.sub('@SOURCE@', source).sub('@HEADER@', header).sub('@DISPLAY_MACROS@', macros).sub('@RESTORE@', text[a...b])
File.write(work + '/fixture.c', harness)

cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror'))
out, err, status = Open3.capture3(*cc, *flags, work + '/fixture.c', '-o', work + '/fixture')
File.write(work + '/compile.log', out + err)
abort out + err unless status.success?
out, err, status = Open3.capture3(work + '/fixture')
File.write(work + '/run.log', out + err)
abort out + err unless status.success?
File.write(work + '/input.sha256', Digest::SHA256.file(source).hexdigest + "  #{source}\n")
puts out
