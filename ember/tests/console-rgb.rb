# Origin: EmberBSD RGB console palette regression, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'sys/dev/rasops/rasops.c'))
body = source[/void\nrasops_set_cmap\(.*?^\}/m]
abort 'FAIL: RGB console ignores programmable palette' unless body
test = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdint.h>
  #include <string.h>
  typedef unsigned int u_int;
  #define LITTLE_ENDIAN 1234
  #define BIG_ENDIAN 4321
  #define BYTE_ORDER LITTLE_ENDIAN
  #define RI_BSWAP 1
  #define bswap16 __builtin_bswap16
  #define bswap32 __builtin_bswap32
  struct rasops_info {
    int ri_depth,ri_flg,ri_rnum,ri_gnum,ri_bnum,ri_rpos,ri_gpos,ri_bpos;
    uint32_t ri_devcmap[256];
  };
  #{body}
  int main(void) {
    struct rasops_info ri = { .ri_depth=32,.ri_rnum=8,.ri_gnum=8,.ri_bnum=8,
      .ri_rpos=16,.ri_gpos=8,.ri_bpos=0 };
    uint8_t r[]={0xff,0x10},g[]={0xab,0x11},b[]={0x42,0x10};
    uint32_t saved[256];
    for(int i=0;i<256;i++) ri.ri_devcmap[i]=0xdeadbeef;
    rasops_set_cmap(&ri,3,2,r,g,b);
    assert(ri.ri_devcmap[3]==0x00ffab42 && ri.ri_devcmap[4]==0x00101110);
    assert(ri.ri_devcmap[2]==0xdeadbeef && ri.ri_devcmap[5]==0xdeadbeef);
    memcpy(saved,ri.ri_devcmap,sizeof(saved));
    rasops_set_cmap(&ri,255,2,r,g,b);assert(memcmp(saved,ri.ri_devcmap,sizeof(saved))==0);
    ri.ri_depth=8;rasops_set_cmap(&ri,3,1,r,g,b);assert(memcmp(saved,ri.ri_devcmap,sizeof(saved))==0);
    ri.ri_depth=32;ri.ri_flg=RI_BSWAP;rasops_set_cmap(&ri,3,1,r,g,b);
    assert(ri.ri_devcmap[3]==0x42abff00);
    ri.ri_flg=0;ri.ri_depth=16;ri.ri_rnum=5;ri.ri_gnum=6;ri.ri_bnum=5;
    ri.ri_rpos=11;ri.ri_gpos=5;rasops_set_cmap(&ri,3,1,r,g,b);
    assert(ri.ri_devcmap[3]==0xfd48fd48);
    ri.ri_flg=RI_BSWAP;rasops_set_cmap(&ri,3,1,r,g,b);assert(ri.ri_devcmap[3]==0x48fd48fd);
    ri.ri_flg=0;ri.ri_depth=24;ri.ri_rnum=ri.ri_gnum=ri.ri_bnum=8;
    ri.ri_rpos=16;ri.ri_gpos=8;rasops_set_cmap(&ri,3,1,r,g,b);assert(ri.ri_devcmap[3]==0xff42abff);
    memcpy(saved,ri.ri_devcmap,sizeof(saved));ri.ri_rnum=0;
    rasops_set_cmap(&ri,3,1,r,g,b);assert(memcmp(saved,ri.ri_devcmap,sizeof(saved))==0);
    return 0;
  }
C
Dir.mktmpdir do |dir|
  file=File.join(dir,'rgb.c');File.write(file,test)
  bin=File.join(dir,'rgb')
  out,err,st=Open3.capture3('cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',file,'-o',bin)
  abort out+err unless st.success?
  out,err,st=Open3.capture3(bin);abort out+err unless st.success?
end
puts 'PASS: actual rasops RGB palette packing, byte order and invalid ranges'
