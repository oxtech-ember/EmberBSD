# Origin: Execute sysinst wizard invalid account feedback, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
s=File.read(root+'/usr.sbin/sysinst/wizard.c')
body=s[/static bool\nsettings\(void\)\n\{.*?^\}/m] or abort 'Missing settings'
code=<<~C
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include "wizard_ui.h"
#define getpwnam test_getpwnam
static char hostname[64]="emberbsd",username[32]="",wizard_timezone[64]="UTC",source_error[128];
static int network_up,choices,inputs;
static const char *invalid;
static struct passwd reserved;
static struct passwd *test_getpwnam(const char *s) {return strcmp(s,"daemon")==0?&reserved:NULL;}
static int choose(const char *s,const char * const *o,int n,int f) {(void)s;(void)o;(void)n;(void)f;return choices++==0?3:4;}
static bool input(const char *s,char *v,size_t n) {
 (void)s;if(inputs++) {assert(source_error[0]);strlcpy(v,"emberuser",n);}else strlcpy(v,invalid,n);return true;
}
static int config_network(int n) {(void)n;return 0;}
#{body}
int main(int argc,char **argv) {
 assert(argc==2);invalid=argv[1];assert(settings());assert(inputs==2);assert(strcmp(username,"emberuser")==0);return 0;
}
C
Dir.mktmpdir do |d|
 f=d+'/settings.c';File.write(f,code);bin=d+'/settings'
 out,err,st=Open3.capture3('cc','-fsanitize=address,undefined','-I'+root+'/usr.sbin/sysinst',f,root+'/usr.sbin/sysinst/wizard_ui.c','-o',bin);abort out+err unless st.success?
 results=%w[Bob daemon].map{|v|out,err,st=Open3.capture3(bin,v);warn out+err unless st.success?;st.success?}
 abort 'FAIL: invalid/reserved username recovery' unless results.all?
end
puts 'PASS: actual settings explain invalid/reserved names and keep the edit open until corrected'
