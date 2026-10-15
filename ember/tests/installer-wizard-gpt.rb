# Origin: Execute sysinst GPT creation against the actual host GPT tool, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root=File.expand_path('../..',__dir__)
tool=ARGV.fetch(0){abort 'Usage: ruby ember/tests/installer-wizard-gpt.rb /absolute/nbgpt'}
s=File.read(root+'/usr.sbin/sysinst/gpt.c')
a=s.index("\tif (!parts->has_gpt) {");b=s.index("\n\t/*\n\t * Delete all old partitions",a)
fragment=s[a...b] or abort 'Missing actual GPT creation branch'
code=<<~C
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#define MAXPATHLEN 1024
static int opendisk(const char *s,int f,char *p,size_t n,int c) {(void)p;(void)n;(void)c;return open(s,f);}
#define RUN_SILENT 1
#define RUN_ERROR_OK 2
static bool wizard_active=true,wizard_automatic=true;
struct gpt_disk_partitions { bool has_gpt;size_t max_num_parts;struct {char *disk;size_t bytes_per_sector;} dp; };
static int run_program(int flags,const char *fmt,...) {
 char command[512],shell[1024];va_list ap;(void)flags;
 va_start(ap,fmt);vsnprintf(command,sizeof(command),fmt,ap);va_end(ap);
 assert(command[0]=='g' && command[1]=='p' && command[2]=='t' && command[3]==' ');
 snprintf(shell,sizeof(shell),"%s %s",getenv("GPT_TOOL"),command+4);
 int rv=system(shell);return WIFEXITED(rv)?WEXITSTATUS(rv):1;
}
static bool create_table(struct gpt_disk_partitions *parts) {
 #{fragment}
 return true;
}
int main(int argc,char **argv) {
 assert(argc==2);struct gpt_disk_partitions p={false,128,{argv[1],512}};
 assert(create_table(&p));assert(p.has_gpt);return 0;
}
C
Dir.mktmpdir do |d|
 File.symlink(tool,d+'/gpt');tool=d+'/gpt'
 f=d+'/create.c';File.write(f,code);bin=d+'/create'
 out,err,st=Open3.capture3('cc','-fsanitize=address,undefined',f,'-o',bin);abort out+err unless st.success?
 results=[]
 %w[blank gpt mbr trailing-space].each do |kind|
  image=d+'/'+kind+'.img';File.open(image,'wb'){|io|io.truncate(16*1024*1024)}
  if kind=='mbr'
   sector="\0".b*512;sector.setbyte(450,169);sector[454,4]=[1].pack('V');sector[458,4]=[1024].pack('V');sector[510,2]="\x55\xaa".b
   File.open(image,'r+b'){|io|io.write(sector)}
  elsif kind!='blank'
   out,err,st=Open3.capture3(tool,'create',image);abort out+err unless st.success?
   File.truncate(image,32*1024*1024) if kind=='trailing-space'
  end
  out,err,st=Open3.capture3({'GPT_TOOL'=>tool},bin,image)
  warn(kind+': '+out+err) unless st.success?;results<<st.success?
  if st.success?
   out,err,st=Open3.capture3({'GPT_TOOL'=>tool},bin,image)
   warn('retry '+kind+': '+out+err) unless st.success?;results<<st.success?
  end
 end
 abort 'FAIL: real GPT create/reset scenarios' unless results.all?
end
puts 'PASS: actual GPT creation branch handles blank, GPT, MBR, trailing space and repeated installation'
