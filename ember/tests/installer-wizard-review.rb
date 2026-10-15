# Origin: Verify the actual review shows the complete automatic partition plan, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
s = ARGV.empty? ? File.read(root+'/usr.sbin/sysinst/wizard.c') : Open3.capture2('git','show',ARGV[0]+':usr.sbin/sysinst/wizard.c',chdir:root).first
body=s[/static int\nreview\(.*?^\}/m] or abort 'Missing actual review'
code=<<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <inttypes.h>
  #include <stdio.h>
  #include <string.h>
  #include "wizard_ui.h"
  #define EMBER_BUILD_ID "0123456789012345678901234567890123456789"
  #define STRSIZE 256
  #define MEG (1024*1024)
  #define PT_EFI_SYSTEM 1
  #define KEY_LEFT 260
  struct scheme {const char *short_name;};
  struct partitions {struct scheme *pscheme;};
  struct part_usage_info {int64_t size;int type,fs_type,fs_version;char mount[32];};
  struct install_partition_desc {size_t num;struct part_usage_info *infos;};
  struct disk {char diskdev_descr[80];struct partitions *parts;unsigned sectorsize;};
  static struct disk *pm;
  static bool net_source;
  static int desktop_index;
  static char source_url[STRSIZE],hostname[]="embertest",username[]="",wizard_timezone[]="UTC";
  static const char *profile="Standard (without X11)";
  static struct wizard_frame captured;
  static int stdscr,keys;
  #define getmaxy(w) (25)
  #define getmaxx(w) (80)
  static bool screen_ready(void) {return true;}
  static int getch(void) {return keys++==0 ? 10 : 'I';}
  static const char *getfslabelname(unsigned t,unsigned v) {(void)t;(void)v;return "swap";}
  static const char *msg_string(const char *s) {return s;}
  static unsigned wizard_optional_omissions(void) {return 2;}
  static void frame(struct wizard_frame *f,const char *s) {wizard_frame_init(f,5,s);}
  static void draw(const struct wizard_frame *f) {captured=*f;}
  #{body}
  int main(void) {
    struct scheme scheme={"GPT"};struct partitions parts={&scheme};
    struct disk disk={"wd0 / 32 GiB / QEMU HARDDISK",&parts,512};pm=&disk;
    struct part_usage_info infos[]={
      {128*2048,1,0,0,""},{30000*2048,0,0,0,"/"},
      {1024*2048,0,0,0,""},{-25,0,0,0,"/tmp"}
    };
    struct install_partition_desc install={4,infos};assert(review(&install)==1);
    char plan[256];snprintf(plan,sizeof(plan),"%s %s",captured.text[11],captured.text[12]);
    assert(strstr(plan,"EFI 128 MiB"));assert(strstr(plan,"/ 30000 MiB"));
    assert(strstr(plan,"swap 1024 MiB"));assert(strstr(plan,"/tmp 25% RAM"));
    assert(strstr(captured.text[19],"WILL BE ERASED"));
    assert(strstr(captured.text[20],"omitted: 2"));
    assert(strstr(captured.text[16],"None / console Wi-Fi"));assert(keys==2);
    return 0;
  }
C
Dir.mktmpdir do |dir|
  File.write(dir+'/review.c',code)
  out,err,st=Open3.capture3('cc','-fsanitize=address,undefined','-I'+root+'/usr.sbin/sysinst',
    dir+'/review.c',root+'/usr.sbin/sysinst/wizard_ui.c','-o',dir+'/review');abort out+err unless st.success?
  out,err,st=Open3.capture3(dir+'/review');abort out+err unless st.success?
end
puts 'PASS: actual review exposes EFI, root, swap and RAM plan; Enter alone cannot approve writes'
