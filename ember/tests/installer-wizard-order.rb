# Origin: Execute the sysinst installation write boundary, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
wizard = File.read(File.join(root,'usr.sbin/sysinst/wizard.c'))
guard = wizard[/bool\nwizard_allow_install\(bool guided\).*?^\}/m] or abort 'Missing early experimental-image guard'
upgrade=File.read(File.join(root,'usr.sbin/sysinst/upgrade.c'))
prefixes=%w[do_upgrade do_reinstall_sets].map do |name|
  prefix=upgrade[/void\n#{name}\(void\)\n\{.*?(?=\n\t(?:msg_display|unwind_mounts)\()/m] or abort "Missing #{name} entry"
  prefix+"\n writes++;\n}\n"
end.join
body = File.read(File.join(root, 'usr.sbin/sysinst/install.c')).split("void\ndo_install(void)\n", 2).fetch(1)
prelude = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdlib.h>
  #include <string.h>
  #define CHECK_ENTROPY 0
  #define NO_PARTMAN 1
  #define MSG_installusure 0
  #define MSG_install 0
  #define MSG_abort_part 0
  #define MSG_swapactive 0
  #define MSG_swapdelfailed 0
  #define MSG_abort_inst 0
  #define MSG_lastchance 0
  #define MSG_disksetupdone 0
  #define MSG_extractcomplete 0
  #define MSG_abortinst 0
  #define MSG_instcomplete 0
  #define MENU_distset 0
  struct install_partition_desc { int dummy; };
  struct pm_stub { bool no_part; void *parts; char diskdev[16]; } disk;
  static struct pm_stub *pm = &disk;
  static bool wizard_active = true, approved, wizard_installing;
  static int partman_go, debug, writes, extracts, prepared, configured, discoveries, notices;
  static void msg_display(int m) { (void)m; }
  static const char *msg_string(int m) { (void)m; return ""; }
  static bool ask_noyes(void *p) { (void)p; return true; }
  static bool wizard_prepare(struct install_partition_desc *p) { (void)p; prepared++; return approved; }
  static bool wizard_configure(void) { configured++; return true; }
  static void wizard_progress(const char *s) { (void)s; }
  static int find_disks(const char *s, bool b) { (void)s; (void)b; discoveries++; return 1; }
  static int partman(struct install_partition_desc *p) { (void)p; return 0; }
  static void hit_enter_to_continue(const char *m, void *p) { (void)m; (void)p; notices++; }
  #{guard}
  static void clear(void) {}
  static void refresh(void) {}
  static int check_swap(const char *s, int n) { (void)s; (void)n; return 0; }
  static bool md_get_info(struct install_partition_desc *p) { (void)p; return true; }
  static int md_make_bsd_partitions(struct install_partition_desc *p) { (void)p; return 1; }
  static void msg_fmt_display(int m, const char *f, const char *s) { (void)m; (void)f; (void)s; }
  static bool write_all_parts(struct install_partition_desc *p) { (void)p; writes++; return true; }
  static int make_filesystems(struct install_partition_desc *p) { (void)p; writes++; return 0; }
  static int make_fstab(struct install_partition_desc *p) { (void)p; return 0; }
  static int md_post_newfs(struct install_partition_desc *p) { (void)p; return 0; }
  static void process_menu(int m, int *r) { (void)m; *r = 1; }
  static int get_and_unpack_sets(int u,int a,int b,int c) { (void)u;(void)a;(void)b;(void)c; extracts++; return 0; }
  static int md_post_extract(struct install_partition_desc *p, bool b) { (void)p;(void)b;return 0; }
  static void root_pw_setup(void) {}
  static void do_configmenu(struct install_partition_desc *p) { (void)p; }
  static void sanity_check(void) {}
  static void md_cleanup_install(struct install_partition_desc *p) { (void)p; }
  static void free_install_desc(struct install_partition_desc *p) { (void)p; }
  void do_install(void)
C
main = <<~C
  #{prefixes}
  int main(void) {
  #ifdef EMBER_BASE_RUNTIME_GCC16
    wizard_active=false;do_install();
    assert(writes==0 && discoveries==0 && notices==1);
    do_upgrade();do_reinstall_sets();assert(writes==0 && notices==3);
  #else
    assert(wizard_allow_install(false));
    do_upgrade();do_reinstall_sets();assert(writes==2 && notices==0);writes=0;
  #endif
    wizard_active=true;
    approved = false; do_install();
    assert(prepared == 1 && writes == 0 && extracts == 0 && configured == 0);
    approved = true; do_install();
    assert(prepared == 2 && writes == 2 && extracts == 1 && configured == 1);
    return 0;
  }
C
Dir.mktmpdir do |tmp|
  source = File.join(tmp, 'order.c')
  File.write(source, prelude + body + main)
  exe = File.join(tmp, 'order')
  if ENV['CPPCHECK']
    out,err,status=Open3.capture3(ENV.fetch('CPPCHECK'),'--enable=warning,performance,portability',
      '--error-exitcode=1','--check-level=exhaustive','--platform=unix64','-DEMBER_BASE_RUNTIME_GCC16=1',source)
    abort out+err unless status.success?
  end
  [[],['-DEMBER_BASE_RUNTIME_GCC16=1']].each do |flags|
    out, err, status = Open3.capture3(ENV.fetch('CC', 'cc'), '-D_DEFAULT_SOURCE',
      *ENV.fetch('CFLAGS','').split,*flags,'-fsanitize=address,undefined', source, '-o', exe)
    abort out + err unless status.success?
    out, err, status = Open3.capture3(exe)
    abort out + err unless status.success?
  end
end
puts 'PASS: actual do_install cancels before writes and executes only an approved wizard plan'
