# Origin: Execute sysinst preflight set selection transactions, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
s=File.read(root+'/usr.sbin/sysinst/util.c')
checker=s[/int\nwizard_check_sets\(const char \*manifest\)\n\{.*?^\}/m] or abort 'Missing preflight'
limiter=s[/void\nwizard_limit_sets\(void\)\n\{.*?^\}/m] || 'void wizard_limit_sets(void) {}'
code=<<~C
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wizard_ui.h"
#define STRSIZE 255
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define SET_GROUP_END 5
#define SET_LAST 3
#define SET_SOURCE 2
#define SET_VALID 1
#define SET_SELECTED 2
#define SET_OK 0
#define SET_RETRY 1
struct distinfo {const char *name;int set;};typedef struct distinfo distinfo;
static distinfo dist_list[]={{"base32",0},{"base",1},{"src",2},{NULL,SET_LAST}};
static uint8_t set_status[SET_GROUP_END],wizard_omitted[SET_GROUP_END];
static char wizard_sums[SET_GROUP_END][129];
static bool wizard_custom;
static int (*fetch_fn)(const char*)=(void*)1;
static int file_exists_p(const char *p) {(void)p;return 1;}
static const char *set_postfix(const char *s){(void)s;return ".tar.xz";}
static const char *ext_dir_for_set(const char *s){(void)s;return "/unused";}
#{checker}
#{limiter}
static void manifest(const char *path,bool compat,bool base,bool malformed) {
 FILE *f=fopen(path,"w");assert(f);
 if(compat)fprintf(f,"SHA512 (base32.tar.xz) = %0128d\\n",0);
 if(malformed)fprintf(f,"SHA512 (base32.tar.xz) = bad\\n");
 if(base)fprintf(f,"SHA512 (base.tar.xz) = %0128d\\n",0);
 fclose(f);
}
int main(int argc,char **argv) {
 assert(argc==2 || argc==3);const char *path=argv[1]; if(argc==3) {set_status[2]=3;wizard_limit_sets();assert(set_status[2]==0);return 0;}
 set_status[0]=set_status[1]=SET_VALID|SET_SELECTED;
 manifest(path,false,false,false);assert(wizard_check_sets(path)==SET_RETRY);
 assert(set_status[0] & SET_SELECTED);
 manifest(path,true,true,false);assert(wizard_check_sets(path)==SET_OK);
 assert(set_status[0] & SET_SELECTED);
 wizard_custom=true;manifest(path,false,true,false);
 assert(wizard_check_sets(path)==SET_RETRY && (set_status[0] & SET_SELECTED));
 wizard_custom=false;manifest(path,false,true,true);
 assert(wizard_check_sets(path)==SET_RETRY && (set_status[0] & SET_SELECTED));
 manifest(path,false,true,false);assert(wizard_check_sets(path)==SET_OK);
 assert(!(set_status[0] & SET_SELECTED));
 manifest(path,true,true,false);assert(wizard_check_sets(path)==SET_OK);
 assert(set_status[0] & SET_SELECTED);
 set_status[2]=SET_VALID|SET_SELECTED;wizard_limit_sets();assert(set_status[2]==0);
 return 0;
}
C
Dir.mktmpdir do |d|
 f=d+'/sets.c';File.write(f,code);bin=d+'/sets'
 out,err,st=Open3.capture3('cc','-fsanitize=address,undefined','-I'+root+'/usr.sbin/sysinst',f,root+'/usr.sbin/sysinst/wizard_ui.c','-o',bin)
 abort out+err unless st.success?
 results=[[],['source']].map do |args|
  out,err,st=Open3.capture3(bin,d+'/SHA512',*args);warn out+err unless st.success?;st.success?
 end
 abort 'FAIL: set transaction/source policy' unless results.all?
end
puts 'PASS: failed preflight preserves requested sets; Custom omissions fail; source sets stay in expert mode'
