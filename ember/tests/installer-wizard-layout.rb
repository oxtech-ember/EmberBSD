# Origin: Execute the automatic sysinst partition planning boundary, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'usr.sbin/sysinst/wizard.c'))
body = source[/static bool\nlayout\(struct install_partition_desc \*install\)\n\{.*?^\}/m] or abort 'Missing layout'
test = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stddef.h>
  #define HAVE_GPT 1
  #define MACH_amd64 1
  struct install_partition_desc { int dummy; };
  struct disk_partitions;
  struct disk_partitioning_scheme {
    struct disk_partitions *(*create_new_for_disk)(const char *,int,int,bool,void *);
    void (*destroy_part_scheme)(struct disk_partitions *);
    void (*free)(struct disk_partitions *);
  };
  struct disk_partitions { const struct disk_partitioning_scheme *pscheme; };
  static int disk_writes, released;
  static void destroy(struct disk_partitions *p) { (void)p;disk_writes++; }
  static void release(struct disk_partitions *p) { (void)p;released++; }
  static struct disk_partitions oldparts, newparts;
  static struct disk_partitions *create(const char *d,int a,int s,bool b,void *p) {
    (void)d;(void)a;(void)s;(void)b;(void)p;return &newparts;
  }
  const struct disk_partitioning_scheme gpt_parts={create,destroy,release};
  static struct { char *diskdev; int dlsize,ptstart,ptsize; struct disk_partitions *parts; } disk={"wd0",100,0,0,&oldparts};
  static typeof(disk) *pm=&disk;
  static bool wizard_automatic;
  static int choose(const char *s,const char * const *c,int n,int i) { (void)s;(void)c;(void)n;(void)i;return 0; }
  static bool md_get_info(struct install_partition_desc *p) { (void)p;return true; }
  static int md_make_bsd_partitions(struct install_partition_desc *p) { (void)p;return 1; }
  #{body}
  int main(void) {
    oldparts.pscheme=&gpt_parts; struct install_partition_desc i={0};
    assert(layout(&i)); assert(disk_writes==0 && released==1 && pm->parts==&newparts);
    return 0;
  }
C
Dir.mktmpdir do |d|
  f=File.join(d,'layout.c');File.write(f,test);bin=File.join(d,'layout')
  out,err,st=Open3.capture3('cc','-std=gnu11','-fsanitize=address,undefined',f,'-o',bin)
  abort out+err unless st.success?
  out,err,st=Open3.capture3(bin);abort out+err unless st.success?
end
puts 'PASS: actual automatic layout releases memory without destroying the existing on-disk table'
