# SPDX-License-Identifier: BSD-2-Clause
# Execute the real boot1 GPT predicate with the current source GUID definitions.
require 'open3'
require 'shellwords'
require 'digest'
require 'fileutils'
root, source, work = ARGV
abort 'Usage: ruby bootxx-gpt-table.rb SOURCE_ROOT BOOT1_SOURCE RECEIPT' unless work
FileUtils.mkdir_p(work)
text = File.read(source)
a = text.index("static int\nis_bootable(") or abort 'Missing bootable predicate'
b = text.index("\n}\n", a) or abort 'Missing predicate end'
uuid_header = File.read(root + '/sys/sys/uuid.h')
uuid = uuid_header[/struct uuid \{.*?\n\};/m] or abort 'Missing uuid layout'
gpt = File.read(root + '/sys/sys/disklabel_gpt.h')
ent = gpt[/struct gpt_ent \{.*?\n\};/m] or abort 'Missing GPT entry layout'
names = %w[UNUSED NETBSD_FFS NETBSD_LFS NETBSD_CCD NETBSD_CGD NETBSD_RAIDFRAME NETBSD_SWAP]
macros = names.map { |name| gpt[/^#define\s+GPT_ENT_TYPE_#{name}\s+\\\n[^\n]*\n/] || abort("Missing GUID #{name}") }.join
code = <<~C
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _UUID_NODE_LEN 6
#define __packed __attribute__((__packed__))
#{uuid}
#{ent}
#{macros}
#{uuid_header[/\A(?:\s|\/\*.*?\*\/)*/m]}
#{gpt[/\A(?:\s|\/\*.*?\*\/)*/m]}
#{text[0...text.index('#include')]}
#{text[a...b + 3]}
#define EXPECT(x) do { if (!(x)) { fprintf(stderr,"FAIL: %s line %d\\n",#x,__LINE__); abort(); } } while (0)
int main(void) {
 const struct uuid yes[] = { GPT_ENT_TYPE_NETBSD_FFS, GPT_ENT_TYPE_NETBSD_LFS, GPT_ENT_TYPE_NETBSD_CCD, GPT_ENT_TYPE_NETBSD_CGD };
 const struct uuid no[] = { GPT_ENT_TYPE_UNUSED, GPT_ENT_TYPE_NETBSD_RAIDFRAME, GPT_ENT_TYPE_NETBSD_SWAP };
 struct gpt_ent e;
 EXPECT(sizeof(struct uuid)==16 && sizeof(e)==128);
 memset(&e,0,sizeof(e));
 for(size_t i=0;i<sizeof(yes)/sizeof(*yes);i++) {
   memcpy(e.ent_type,&yes[i],sizeof(yes[i])); EXPECT(is_bootable(&e)==1);
   for(size_t byte=0;byte<sizeof(yes[i]);byte++) {
     e.ent_type[byte]^=0x80; EXPECT(is_bootable(&e)==0); e.ent_type[byte]^=0x80;
   }
 }
 for(size_t i=0;i<sizeof(no)/sizeof(*no);i++) {memcpy(e.ent_type,&no[i],sizeof(no[i]));EXPECT(is_bootable(&e)==0);}
 memset(e.ent_type,0xff,sizeof(e.ent_type));EXPECT(is_bootable(&e)==0);
 puts("PASS four bootable GUIDs, 64 altered GUIDs, unused/RAID/swap/unknown and actual 16/128-byte layouts");
 return 0;
}
C
File.write(work + '/fixture.c', code)
cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wmissing-prototypes -Wstrict-prototypes -Werror'))
out, err, status = Open3.capture3(*cc, *flags, work + '/fixture.c', '-o', work + '/fixture')
File.write(work + '/compile.log', out + err)
abort out + err unless status.success?
out, err, status = Open3.capture3(work + '/fixture')
File.write(work + '/run.log', out + err)
abort out + err unless status.success?
File.write(work + '/input.sha256', [source, root + '/sys/sys/uuid.h', root + '/sys/sys/disklabel_gpt.h'].map { |p| Digest::SHA256.file(p).hexdigest + "  #{p}\n" }.join)
puts out
