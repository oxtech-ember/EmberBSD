# Origin: EmberBSD desktop preflight regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
source=File.read(root+'/usr.sbin/sysinst/wizard.c')
body=source[/static bool\ndesktop_preflight\(const char \*manifest\)\n\{.*?^\}/m] or abort 'Missing desktop preflight before disk writes'
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
static int desktop_index,commands,mounts;
static bool net_source,desktop_mounted;
static char desktop_cache[STRSIZE];
static char desktop_bundle[STRSIZE],desktop_sum[129],ext_dir_bin[STRSIZE],source_url[STRSIZE];
static int run_program(int flags,const char *format,...) {(void)flags;if(strstr(format,"mount_tmpfs")){mounts++;return 0;}if(strstr(format,"umount"))return 0;commands++;return 1;}
static int test_statvfs(const char *p,struct statvfs *st) {(void)p;memset(st,0,sizeof(*st));st->f_frsize=MEG;st->f_bavail=16;return 0;}
#define statvfs(p,s) test_statvfs(p,s)
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
 assert(!feof(f)); return fread(p,size,n,f);
}
#define fread checked_fread
#{body}
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
 return 0;
}
C
Dir.mktmpdir do |d|
  File.write(d+'/desktop.c',code)
  libs=RUBY_PLATFORM.include?('darwin') ? [] : ['-lcrypto']
  out,err,st=Open3.capture3('cc','-Wno-deprecated-declarations','-Werror=sign-compare','-fsanitize=address,undefined','-I'+root+'/usr.sbin/sysinst',d+'/desktop.c',root+'/usr.sbin/sysinst/wizard_ui.c',*libs,'-o',d+'/desktop');abort out+err unless st.success?
  out,err,st=Open3.capture3(d+'/desktop',d);abort out+err unless st.success?
end
puts 'Desktop preflight: absent bundle, checksum mismatch, failed fetch and cached-byte isolation pass'
