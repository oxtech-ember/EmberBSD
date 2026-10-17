# SPDX-License-Identifier: BSD-2-Clause
# Query the real sysinst make rules. Empty stage files are metadata fixtures only.
require 'tmpdir'
require 'fileutils'
require 'open3'
wrapper=File.expand_path(ARGV.fetch(0){abort 'Usage: ruby installer-runtime-build.rb /absolute/nbmake-amd64'})
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('ember-runtime-build-') do |d|
  stage=d+'/stage'
  files=%w[lib/libgcc_s.so.1 lib/libstdc++.so lib/libstdc++.so.7 lib/libstdc++.a lib/libsupc++.a
    include/c++/vector include/c++/x86_64--netbsd/bits/c++config.h]
  files+=%w[crtbegin.o crtbeginS.o crtend.o crtendS.o libgcc.a libgcc_eh.a].map{|f|'lib/gcc/x86_64--netbsd/16.2.0/'+f}
  files.each{|f|FileUtils.mkdir_p(File.dirname(stage+'/'+f));File.write(stage+'/'+f,'')}
  [[],['EMBER_EXTERNAL_GCC_RUNTIME='+stage]].each do |flags|
    out,err,st=Open3.capture3(wrapper,'-m',root+'/share/mk','-C',root+'/usr.sbin/sysinst/arch/amd64',
      'NETBSDOBJDIR='+d+'/obj',*flags,'-V','CPPFLAGS')
    abort out+err unless st.success?
    abort 'Incorrect automatic runtime-image policy' unless out.include?('-DEMBER_BASE_RUNTIME_GCC16')==!flags.empty?
  end
end
puts 'PASS: real sysinst make rules enable required-runtime policy only for external runtime'
