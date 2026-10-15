# Origin: EmberBSD desktop preflight regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
source=File.read(root+'/usr.sbin/sysinst/wizard.c')
body=source[/static bool\ndesktop_preflight\(const char \*manifest\)\n\{.*?^\}/m] or abort 'Missing desktop preflight before disk writes'
freeze=source[/static bool\nbundle_preflight\(.*?^\}/m] or abort 'Missing shared bounded bundle preflight'
clear=source[/static bool\nbundle_clear\(.*?^\}/m] or abort 'Missing shared bundle cleanup'
runtime=source[/static bool\nruntime_preflight\(.*?^\}/m] or abort 'Missing mandatory runtime preflight'
apply=source[/bool\nwizard_apply_runtime\(void\).*?^\}/m] or abort 'Missing runtime apply before postinstall'
util=File.read(root+'/usr.sbin/sysinst/util.c')
finish=util[/\t\/\* The base runtime.*?(?=\n\tif \(!update\) \{\n\t\tstruct stat sb1)/m] or abort 'Missing post-extraction runtime boundary'
abort 'Desktop is not selectable within System' unless source.include?('Graphics:')
code=<<~C
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/statvfs.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#define CC_SHA512_CTX SHA512_CTX
#define CC_SHA512_DIGEST_LENGTH SHA512_DIGEST_LENGTH
#define CC_SHA512_Init SHA512_Init
#define CC_SHA512_Update SHA512_Update
#define CC_SHA512_Final SHA512_Final
typedef size_t CC_LONG;
#endif
#include "wizard_ui.h"
#define STRSIZE 1024
#define MEG (1024UL*1024UL)
#define RUN_SILENT 1
#define RUN_ERROR_OK 2
#define RUN_PROGRESS 4
#define RUN_CHROOT 8
#define EMBER_BASE_RUNTIME_GCC16 1
#define EMBER_BUILD_ID "fixture-build"
#define SET_OK 0
static int desktop_index,commands,mounts;
static bool net_source,desktop_mounted,runtime_mounted,corrupt_copy;
static char desktop_cache[STRSIZE];
static char desktop_bundle[STRSIZE],desktop_sum[129],ext_dir_bin[STRSIZE],source_url[STRSIZE];
static char runtime_bundle[STRSIZE],runtime_sum[129],runtime_cache[STRSIZE],source_error[128];
static bool wizard_active=true;
static bool runtime_installed,entropy_loaded;
static int configured;
static int run_program(int flags,const char *format,...) {
 (void)flags;if(strstr(format,"mount_tmpfs")){mounts++;return 0;}if(strstr(format,"umount"))return 0;
 if(strstr(format,"runtime") && (getenv("LD_LIBRARY_PATH") || getenv("LD_PRELOAD")))abort();
 if(strstr(format,"ember-runtime-install"))runtime_installed=true;
 if(strstr(format,"postinstall") || strstr(format,"certctl")){assert(runtime_installed);configured++;}
 commands++;return strstr(format,"ftp")?1:0;
}
static void wizard_progress(const char *p) {(void)p;}
static int wizard_check_sets(const char *p) {(void)p;return SET_OK;}
static char target[STRSIZE];
static const char *target_expand(const char *p) {static char path[STRSIZE];snprintf(path,sizeof(path),"%s%s",target,p);return path;}
static void make_target_dir(const char *p) {(void)p;}
static int cp_to_target(const char *from,const char *to) {
 FILE *in=fopen(from,"rb"),*out=fopen(target_expand(to),"wb");assert(in && out);
 int c;while((c=fgetc(in))!=EOF)fputc(c,out);if(corrupt_copy)fputc('!',out);fclose(in);return fclose(out);
}
static int test_statvfs(const char *p,struct statvfs *st) {(void)p;memset(st,0,sizeof(*st));st->f_frsize=MEG;st->f_bavail=16;return 0;}
#define statvfs(p,s) test_statvfs(p,s)
#{clear}
#{source[/static (?:bool|void)\ndesktop_clear\(void\)\n\{.*?^\}/m]}
static char *SHA512_File(const char *path,char *hex) {
 unsigned char digest[CC_SHA512_DIGEST_LENGTH],buf[4096];CC_SHA512_CTX ctx;
 FILE *f=fopen(path,"rb");if(!f)return NULL;CC_SHA512_Init(&ctx);size_t n;
 for(;;){n=fread(buf,1,sizeof(buf),f);if(ferror(f)){fclose(f);return NULL;}
 if(n==0)break;CC_SHA512_Update(&ctx,buf,(CC_LONG)n);if(feof(f))break;}
 fclose(f);CC_SHA512_Final(digest,&ctx);
 for(int i=0;i<64;i++)sprintf(hex+2*i,"%02x",digest[i]);return hex;
}
static size_t checked_fread(void *p,size_t size,size_t n,FILE *f) {
 if(feof(f))abort();return fread(p,size,n,f);
}
#define fread checked_fread
#{freeze}
#{body}
#{source[/static void\nruntime_clear\(void\).*?^\}/m]}
#{runtime}
#{apply}
#{source[/static bool\npreflight\(void\).*?^\}/m]}
static struct {int nerror,nsuccess,nselected;} tarstats;
#define SET_ETC 0
#define SET_BASE 1
#define SET_INSTALLED 1
#define MSG_oldsendmail 0
#define RUN_DISPLAY 16
static int set_status[2]={SET_INSTALLED,SET_INSTALLED};
static void msg_display(int m) {(void)m;}
static bool ask_yesno(void *p) {(void)p;return false;}
static void run_makedev(void) {assert(runtime_installed);configured++;}
static int finish_sets(bool update) {
#{finish}
 return 0;
}
/* Keep the real preflight/controller/write jump; replace UI and disk I/O. */
#define SET_ABANDON 3
#define SFLAG_NOX 1
#define SFLAG_MINIMAL 2
struct install_partition_desc {int dummy;};
static int stage,partman_go,profile_index,discoveries,source_calls,reviewed,writes;
static bool profile_ready,wizard_installing,wizard_custom,wizard_expert_requested;
static char boot_medium[128];
static const char *profile;
static void free_install_desc(struct install_partition_desc *p) {(void)p;}
static int find_disks(const char *p,bool b) {(void)p;(void)b;return discoveries++==0?1:-1;}
static bool layout(struct install_partition_desc *p) {(void)p;return true;}
static int select_source(void) {return source_calls++==0?SET_OK:SET_ABANDON;}
static int choose(const char *p,const char * const *v,int n,int first) {(void)p;(void)v;(void)n;(void)first;return 1;}
static void init_set_status(int p) {(void)p;}
static void wizard_limit_sets(void) {}
static void wizard_desktop_sets(bool enabled) {(void)enabled;}
static void customise_sets(void) {}
static bool settings(void) {return true;}
static int review(struct install_partition_desc *p) {(void)p;reviewed++;return 1;}
static void hit_enter_to_continue(const char *p,void *v) {(void)p;(void)v;}
#{source[/bool\nwizard_allow_install\(bool guided\).*?^\}/m]}
#{source[/bool\nwizard_prepare\(struct install_partition_desc \*install\).*?^\}/m]}
#{File.read(root+'/usr.sbin/sysinst/install.c')[/void\ndo_install\(void\)\n\{.*?(?=\n#ifndef NO_PARTMAN)/m]}
 return;
write_plan: writes++;
error: free_install_desc(&install);
}
#undef fread
int main(int argc,char **argv) {
 assert(argc==2);strlcpy(ext_dir_bin,argv[1],sizeof(ext_dir_bin));
 char manifest[STRSIZE],bundle[STRSIZE],sum[129];
 snprintf(manifest,sizeof(manifest),"%s/SHA512",argv[1]);
 snprintf(bundle,sizeof(bundle),"%s/desktop-xfce.tar.xz",argv[1]);
 assert(desktop_preflight(manifest));assert(commands==0);
 desktop_index=1;assert(!desktop_preflight(manifest));
 FILE *f=fopen(bundle,"w");assert(f);fputs("verified package fixture",f);fclose(f);
 assert(SHA512_File(bundle,sum));f=fopen(manifest,"w");assert(f);
 fprintf(f,"SHA512 (desktop-xfce.tar.xz) = %s\\n",sum);fclose(f);
 assert(desktop_preflight(manifest));assert(strcmp(desktop_bundle,bundle)!=0);assert(mounts==1);
 f=fopen(bundle,"w");assert(f);fputs("changed source",f);fclose(f);
 assert(SHA512_File(desktop_bundle,sum)); /* cached bytes survive source mutation */
 assert(!desktop_preflight(manifest));assert(desktop_bundle[0]==0);
 net_source=true;assert(!desktop_preflight(manifest));assert(commands==1);
 assert(desktop_bundle[0]==0);
 net_source=false;desktop_index=2;
 assert(!desktop_preflight(manifest)); /* Never accept XFCE for Plasma. */
 snprintf(bundle,sizeof(bundle),"%s/desktop-plasma.tar.xz",argv[1]);
 f=fopen(bundle,"w");assert(f);fputs("Plasma package fixture",f);fclose(f);
 assert(SHA512_File(bundle,sum));f=fopen(manifest,"w");assert(f);
 fprintf(f,"SHA512 (desktop-plasma.tar.xz) = %s\\n",sum);fclose(f);
 assert(desktop_preflight(manifest));assert(strcmp(desktop_sum,sum)==0);
 desktop_index=3;assert(!desktop_preflight(manifest));
 desktop_index=-1;assert(!desktop_preflight(manifest));
 desktop_index=0;assert(desktop_preflight(manifest));
 /* The mandatory runtime uses the same freeze independently of desktop. */
 char runtime[STRSIZE]={0},runtime_sum[129]={0},cache[STRSIZE]={0};bool mounted=false;
 snprintf(bundle,sizeof(bundle),"%s/runtime-gcc16.tar.xz",argv[1]);
 assert(!bundle_preflight(manifest,"runtime-gcc16.tar.xz",64*MEG,runtime,runtime_sum,cache,&mounted));
 f=fopen(bundle,"w");assert(f);fputs("runtime package fixture",f);fclose(f);
 assert(SHA512_File(bundle,sum));f=fopen(manifest,"w");assert(f);
 fprintf(f,"SHA512 (runtime-gcc16.tar.xz) = %s\\n",sum);fclose(f);
 assert(bundle_preflight(manifest,"runtime-gcc16.tar.xz",64*MEG,runtime,runtime_sum,cache,&mounted));
 assert(strcmp(runtime_sum,sum)==0);assert(strcmp(runtime,bundle)!=0);
 f=fopen(bundle,"w");assert(f);int rv=ftruncate(fileno(f),64*MEG+1);assert(rv==0);fclose(f);
 assert(!bundle_preflight(manifest,"runtime-gcc16.tar.xz",64*MEG,runtime,runtime_sum,cache,&mounted));
 assert(runtime[0]==0 && runtime_sum[0]==0);
 /* Execute the actual source preflight with None, without package bytes. */
 char identity[STRSIZE];snprintf(identity,sizeof(identity),"%s/BUILDID",argv[1]);
 f=fopen(identity,"w");assert(f);fputs("fixture-build\\n",f);fclose(f);
 assert(!preflight());assert(strstr(source_error,"Build check failed"));
 do_install();assert(writes==0 && reviewed==0); /* Missing runtime cannot reach GPT. */
 f=fopen(bundle,"w");assert(f);fputs("runtime package fixture",f);fclose(f);
 assert(SHA512_File(bundle,sum));f=fopen(manifest,"w");assert(f);
 fprintf(f,"SHA512 (runtime-gcc16.tar.xz) = %s\\n",sum);fclose(f);
 discoveries=source_calls=0;do_install();assert(writes==1 && reviewed==1);assert(runtime_bundle[0]);
 snprintf(target,sizeof(target),"%s/target",argv[1]);rv=mkdir(target,0700);assert(rv==0);
 char directory[STRSIZE];snprintf(directory,sizeof(directory),"%s/var",target);rv=mkdir(directory,0700);assert(rv==0);
 snprintf(directory,sizeof(directory),"%s/var/cache",target);rv=mkdir(directory,0700);assert(rv==0);
 commands=0;tarstats.nselected=1;tarstats.nsuccess=0;
 rv=setenv("LD_LIBRARY_PATH","/usr/pkg/gcc16/lib",1);assert(rv==0);
 rv=setenv("LD_PRELOAD","/foreign.so",1);assert(rv==0);
 assert(finish_sets(false)==1);assert(commands==0 && configured==0);
 tarstats.nsuccess=1;corrupt_copy=true;assert(finish_sets(false)==1);assert(commands==0 && configured==0);
 corrupt_copy=false;assert(finish_sets(false)==0);assert(commands==3 && configured==2);assert(runtime_bundle[0]==0);
 assert(preflight());runtime_installed=false;configured=0;
 assert(finish_sets(true)==0);assert(configured==3);assert(entropy_loaded);
 return 0;
}
C
Dir.mktmpdir do |d|
  File.write(d+'/desktop.c',code)
  libs=RUBY_PLATFORM.include?('darwin') ? [] : ['-lcrypto']
  if ENV['CPPCHECK']
    out,err,st=Open3.capture3(ENV.fetch('CPPCHECK'),'--enable=warning,performance,portability','--error-exitcode=1','--check-level=exhaustive','--library=posix','--platform=unix64','-D__APPLE__','-I'+root+'/usr.sbin/sysinst',d+'/desktop.c');abort out+err unless st.success?
  end
  out,err,st=Open3.capture3(ENV.fetch('CC','cc'),*ENV.fetch('CFLAGS','').split,'-Wno-deprecated-declarations','-Werror=sign-compare','-fsanitize=address,undefined','-I'+root+'/usr.sbin/sysinst',d+'/desktop.c',root+'/usr.sbin/sysinst/wizard_ui.c',*libs,'-o',d+'/desktop');abort out+err unless st.success?
  out,err,st=Open3.capture3(d+'/desktop',d);abort out+err unless st.success?
end
puts 'PASS: real preflight/controller refuses missing runtime before GPT; bounded freeze, target hash and pre-postinstall ordering'
