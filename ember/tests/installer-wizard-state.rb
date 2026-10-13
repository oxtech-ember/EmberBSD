# Origin: Execute the sysinst wizard retry state, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'usr.sbin/sysinst/wizard.c'))
body = source[/bool\nwizard_prepare\(struct install_partition_desc \*install\)\n\{.*?^\}/m] or abort 'Missing controller'
test = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <string.h>
  #include <stdio.h>
  #define SET_ABANDON 3
  #define SET_OK 0
  #define SFLAG_NOX 1
  #define SFLAG_MINIMAL 2
  struct install_partition_desc { int dummy; };
  static int stage, partman_go, choice_calls, preflights, initializations, selected;
  static int profile_index,desktop_index;
  static bool profile_ready, wizard_installing, wizard_custom, wizard_expert_requested;
  static char boot_medium[128];
  static const char *profile;
  static void free_install_desc(struct install_partition_desc *i) { (void)i; }
  static int find_disks(const char *s,bool b) { (void)s;(void)b;return 1; }
  static bool layout(struct install_partition_desc *i) { (void)i;return true; }
  static int select_source(void) { return SET_OK; }
  static int choose(const char *s,const char * const *p,int n,int first) {
    (void)s;(void)p;(void)n;return choice_calls++==0?2:first;
  }
  static void init_set_status(int p) { (void)p;initializations++;selected=0; }
  static void wizard_limit_sets(void) {}
  static void wizard_desktop_sets(bool enabled) {(void)enabled;}
  static void customise_sets(void) { selected=99; }
  static bool settings(void) { return true; }
  static bool preflight(void) { return preflights++!=0; }
  static int review(struct install_partition_desc *i) { (void)i;return 1; }
  #{body}
  int main(void) {
    struct install_partition_desc i={0};assert(wizard_prepare(&i));
    assert(choice_calls==2 && initializations==1 && selected==99);
    assert(strcmp(profile,"Custom")==0);
    return 0;
  }
C
Dir.mktmpdir do |d|
  f=File.join(d,'state.c');File.write(f,test);bin=File.join(d,'state')
  out,err,st=Open3.capture3('cc','-fsanitize=address,undefined',f,'-o',bin)
  abort out+err unless st.success?
  out,err,st=Open3.capture3(bin);abort out+err unless st.success?
end
puts 'PASS: actual wizard retains the custom profile and set selection after a failed source check'
