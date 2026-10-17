# SPDX-License-Identifier: BSD-2-Clause
# Exercise the installed runtime verifier. Target pkgtools are boundary doubles.
require 'tmpdir'
require 'fileutils'
require 'digest'
require 'open3'
root=File.expand_path('../..',__dir__)
path=root+'/libexec/ember-desktop/ember-runtime-install'
abort 'Missing runtime installer' unless File.file?(path)
Dir.mktmpdir('ember-runtime-helper-') do |d|
  FileUtils.mkdir_p(d+'/cache/All')
  package='gcc16-runtime-16.2.0nb1'
  File.binwrite(d+"/cache/All/#{package}.tgz",'package fixture')
  hash=Digest::SHA512.file(d+"/cache/All/#{package}.tgz").hexdigest
  valid={'PROFILE'=>"gcc16\n",'ROOTS'=>package+"\n",'SHA512'=>"SHA512 (#{package}.tgz) = #{hash}\n"}
  valid.each{|n,v|File.write(d+'/cache/'+n,v)}
  File.write(d+'/pkg_add',"#!/bin/sh\n[ -z \"${LD_LIBRARY_PATH-}${LD_PRELOAD-}\" ] || exit 43\n[ \"$PKG_DBDIR\" = /usr/pkg/pkgdb ] || exit 42\nprintf '%s\\n' \"$1\" >> '#{d}/added'\nexit ${ADD_FAIL:-0}\n")
  File.write(d+'/pkg_info',"#!/bin/sh\ncase \"$2\" in gcc16-runtime-*) [ -f '#{d}/added' ];; *) [ \"${CONFLICT:-0}\" = 1 ];; esac\n")
  File.write(d+'/sha512',"#!/bin/sh\nshasum -a 512 \"$2\" | cut -d ' ' -f1\n")
  %w[pkg_add pkg_info sha512].each{|n|File.chmod(0755,d+'/'+n)}
  script=File.read(path).gsub('/var/cache/ember-runtime',d+'/cache').sub('$(id -u)','0')
  %w[pkg_add pkg_info sha512].each{|n|script=script.gsub('/usr/sbin/'+n,d+'/'+n).gsub('/usr/bin/'+n,d+'/'+n)}
  run=->(env={}){Open3.capture3(env,'sh','-eu','-c',script)}
  {'PROFILE'=>"plasma\n",'ROOTS'=>"gcc16-16.2.0nb1\n",'SHA512'=>"SHA512 (#{package}.tgz) = #{'0'*128}\n"}.each do |n,bad|
    File.write(d+'/cache/'+n,bad)
    out,err,st=run.call
    abort "Accepted malformed #{n}: #{out}#{err}" if st.success? || File.exist?(d+'/added')
    File.write(d+'/cache/'+n,valid.fetch(n))
  end
  out,err,st=run.call({'CONFLICT'=>'1'})
  abort 'Conflicting provider reached pkg_add' if st.success? || File.exist?(d+'/added')
  out,err,st=run.call({'ADD_FAIL'=>'17'})
  abort 'pkg_add failure was accepted' if st.success?
  FileUtils.rm_f(d+'/added')
  out,err,st=run.call({'LD_LIBRARY_PATH'=>'/foreign','LD_PRELOAD'=>'/foreign.so'})
  abort out+err unless st.success? && File.read(d+'/added')==d+"/cache/All/#{package}.tgz\n"
end
puts 'PASS: exact runtime profile/package/checksum, provider refusal and pkg_add status'
