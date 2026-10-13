# Origin: EmberBSD boot medium protection contract, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'usr.sbin/sysinst/wizard.c'))
body = source[/bool\nwizard_disk_is_root\(const char \*dev\)\n\{.*?^\}/m]
abort 'FAIL: wizard has no whole-parent boot disk protection' unless body
test = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdio.h>
  #include <string.h>
  #define SSTRSIZE 30
  #define STRSIZE 255
  #define CTL_KERN 1
  #define KERN_ROOT_DEVICE 2
  #define O_RDONLY 0
  #define DIOCGWEDGEINFO 3
  struct dkwedge_info { char dkw_parent[16]; };
  static const char *rootdev;
  static bool fail;
  static int sysctl(int *a,int b,void *c,size_t *d,void *e,int f) {
    (void)a;(void)b;(void)d;(void)e;(void)f; strcpy(c,rootdev);return 0;
  }
  static int open(const char *p,int f) { (void)p;(void)f;return fail?-1:4; }
  static int ioctl(int fd,int r,struct dkwedge_info *w) { (void)fd;(void)r;strcpy(w->dkw_parent,"sd0");return 0; }
  static int close(int fd) { (void)fd;return 0; }
  #{body}
  int main(void) {
    rootdev="sd0";assert(wizard_disk_is_root("sd0"));assert(!wizard_disk_is_root("sd01"));
    rootdev="dk0";assert(wizard_disk_is_root("sd0"));assert(!wizard_disk_is_root("wd0"));
    fail=true;assert(wizard_disk_is_root("wd0"));
    return 0;
  }
C
Dir.mktmpdir do |d|
  f = File.join(d, 'disk.c'); File.write(f, test)
  bin = File.join(d, 'disk')
  out, err, st = Open3.capture3('cc', '-fsanitize=address,undefined', f, '-o', bin)
  abort out + err unless st.success?
  out, err, st = Open3.capture3(bin); abort out + err unless st.success?
end
puts 'PASS: USB root, GPT wedge parent, exact disk names and fail-closed detection'
