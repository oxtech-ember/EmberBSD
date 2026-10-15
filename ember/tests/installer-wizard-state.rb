# Origin: Execute the sysinst wizard retry state, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'usr.sbin/sysinst/wizard.c'))
body = source[/bool\nwizard_prepare\(struct install_partition_desc \*install\)\n\{.*?^\}/m] or abort 'Missing controller'
sets = File.read(File.join(root,'usr.sbin/sysinst/util.c'))[/void\nwizard_desktop_sets\(bool enabled\).*?^\}/m] or abort 'Missing X11 selection'
test = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <string.h>
  #include <stdio.h>
  #define SET_ABANDON 3
  #define SET_OK 0
  #define SFLAG_NOX 1
  #define SFLAG_MINIMAL 2
  #define SET_X11_FIRST 0
  #define SET_X11_LAST 6
  #define SET_SELECTED 2
  struct install_partition_desc { int dummy; };
  static int stage, partman_go, choice_calls, preflights, initializations, selected;
  static int profile_index,desktop_index,graphics_calls;
  static int selected_profile,selected_desktop;
  static unsigned set_status[6];
  static bool profile_ready, wizard_installing, wizard_custom, wizard_expert_requested;
  static char boot_medium[128];
  static const char *profile;
  static void free_install_desc(struct install_partition_desc *i) { (void)i; }
  static int find_disks(const char *s,bool b) { (void)s;(void)b;return 1; }
  static bool layout(struct install_partition_desc *i) { (void)i;return true; }
  static int select_source(void) { return SET_OK; }
  static int choose(const char *s,const char * const *p,int n,int first) {
    (void)p;(void)n;
    if (strcmp(s,"Choose your graphics environment")==0) return selected_desktop;
    if (graphics_calls++==0) return 3;
    return choice_calls++==0?selected_profile:first;
  }
  static void init_set_status(int p) { (void)p;initializations++;selected=0;memset(set_status,0,sizeof(set_status)); }
  static void wizard_limit_sets(void) {}
  #{sets}
  static void customise_sets(void) { selected=99;set_status[2]=SET_SELECTED; }
  static bool settings(void) { return true; }
  static bool preflight(void) { return preflights++!=0; }
  static int review(struct install_partition_desc *i) { (void)i;return 1; }
  #{body}
  int main(void) {
    for(int scenario=0;scenario<4;scenario++) {
      selected_profile=scenario==3?2:1;selected_desktop=scenario==3?2:scenario;
      choice_calls=initializations=graphics_calls=preflights=0;
      struct install_partition_desc i={0};assert(wizard_prepare(&i));
      assert(choice_calls==2 && initializations==1);
      assert(desktop_index==selected_desktop);
      for(int n=1;n<6;n++) {
        bool want=selected_desktop==1 || (selected_profile==2 && n==2);
        assert(!!(set_status[n] & SET_SELECTED)==want);
      }
      if(selected_profile==2)assert(selected==99 && strcmp(profile,"Custom")==0);
      else assert(strcmp(profile,"Minimal")==0);
    }
    return 0;
  }
C
Dir.mktmpdir do |d|
  f=File.join(d,'state.c');File.write(f,test);bin=File.join(d,'state')
  if ENV['CPPCHECK']
    out,err,st=Open3.capture3(ENV.fetch('CPPCHECK'),'--enable=warning,performance,portability','--error-exitcode=1','--check-level=exhaustive','--platform=unix64',f)
    abort out+err unless st.success?
  end
  out,err,st=Open3.capture3(ENV.fetch('CC','cc'),*ENV.fetch('CFLAGS','').split,'-fsanitize=address,undefined',f,'-o',bin)
  abort out+err unless st.success?
  out,err,st=Open3.capture3(bin);abort out+err unless st.success?
end
puts 'PASS: real wizard keeps Plasma/None free of forced base X11; XFCE keeps five sets; Custom and retries preserve explicit selections'
