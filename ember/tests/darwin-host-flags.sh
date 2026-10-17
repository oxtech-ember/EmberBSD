#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted real Makefile/compiler regression.
set -eu
[ "$#" = 6 ] || { echo 'usage: darwin-host-flags.sh SOURCE MAKE GCC CLANG SDK NEW_WORK' >&2; exit 2; }
exec ruby - "$@" <<'RUBY'
require 'fileutils'
require 'open3'
require 'shellwords'
src,make,gcc,clang,sdk,work=ARGV
abort 'NEW_WORK exists' if File.exist?(work)
FileUtils.mkdir_p(work)
input=File.join(work,'input.c')
File.write(input,"int main(void) { return 0; }\n")
dirs=%w[tools/compat tools/libctf tools/elftoolchain/libdwarf
        tools/elftoolchain/libelf tools/libmopcopy tools/binutils]
passed=0
failed=0
dirs.each do |dir|
  {'gcc'=>gcc,'clang'=>clang}.each do |family,cc|
    %w[Darwin Linux].each do |host|
      name="#{dir.tr('/','-')}-#{family}-#{host}"
      out,err,status=Open3.capture3(make,'-C',File.join(src,dir),
        "NETBSDSRCDIR=#{src}","HOST_CC=#{cc}","BUILD_OSTYPE=#{host}",
        '-V','${HOST_CPPFLAGS} ${HOST_CFLAGS} ${CPPFLAGS}')
      File.write(File.join(work,"#{name}.make.log"),out+err)
      abort "Makefile failed #{name}" unless status.success?
      flags=Shellwords.split(out)
      # bsd.hostlib.mk discards libmopcopy's legacy CPPFLAGS assignment.
      expected=(host=='Darwin' && family=='clang' && dir!='tools/libmopcopy')
      correct=(flags.include?('-no-cpp-precomp')==expected)
      out,err,status=Open3.capture3({'SDKROOT'=>sdk},cc,*flags,'-c',input,
        '-o',File.join(work,"#{name}.o"))
      File.write(File.join(work,"#{name}.compile.log"),out+err)
      if correct && status.success?
        passed+=1; puts "PASS #{name}"
      else
        failed+=1; puts "FAIL #{name}: flag=#{correct}, compile=#{status.exitstatus}"
      end
    end
  end
end
puts "host flags: #{passed} passed, #{failed} failed (24 real Makefile/compiler cases)"
exit(passed==24 && failed==0 ? 0 : 1)
RUBY
