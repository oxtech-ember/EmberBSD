# Origin: Execute sysinst guided discovery and menu adapters, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
s=File.read(root+'/usr.sbin/sysinst/disks.c')
start=s.index("\t\t\tif (wizard_active) {",s.index('find_disks(const'))
finish=s.index("\n\t\t\tw_menu_no = -1;",start)
block=s[start...finish]
probe=s[(s.index('/* Fill pm struct'))..][/pm->parts = .*?;/m]
abort 'Missing actual adapters' unless block && probe
code=<<~C
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define MAX_DISKS 32
static bool wizard_active=true;
static int i, selected_disk, dno=3, disk_no[]={0,2,-2};
static struct {const char *opt_name;} dsk_menu[]={ {"wd0"},{"ld0"},{(const char*)1} };
static const char *msg_string(const char *p) { return p; }
static int wizard_disk_choice(const char * const *n,int count) {
 assert(count==2);assert(strcmp(n[0],"wd0")==0);assert(strcmp(n[1],"ld0")==0);return 1;
}
static int choose_disk(void) {
 #{block}
 disk_chosen: return selected_disk;
}
static int writes;
static struct {int dd_secsize;bool dd_no_mbr;} disk_value,*disk=&disk_value;
static struct {const char *diskdev;int dlsize;void *parts;} pm_value,*pm=&pm_value;
static void *partitions_read_disk(const char *s,int n,int z,bool b) {
 (void)s;(void)n;(void)z;(void)b;writes++;return NULL;
}
int main(int argc,char **argv) {
 if(argc>1) { assert(choose_disk()==2);return 0; } (void)argv;
 #{probe}
 assert(writes==0);wizard_active=false;
 #{probe}
 assert(writes==1);return 0;
}
C
Dir.mktmpdir do |d|
 f=d+'/adapter.c';File.write(f,code);bin=d+'/adapter'
 out,err,st=Open3.capture3('cc','-fsanitize=address,undefined',f,'-o',bin);abort out+err unless st.success?
 results=[[],['menu']].map do |args|
  out,err,st=Open3.capture3(bin,*args);warn(out+err) unless st.success?;st.success?
 end
 abort 'FAIL: discovery/menu adapters' unless results.all?
end
puts 'PASS: actual guided discovery skips the mutating partition reader and omits the wedge message ID'
