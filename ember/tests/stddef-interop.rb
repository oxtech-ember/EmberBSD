# SPDX-License-Identifier: BSD-2-Clause
# Exercise the production stddef header; amd64 target mode fixes the ABI expectations.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
cc = Shellwords.split(ENV.fetch('STDDEF_CC', 'cc'))
cxx = Shellwords.split(ENV.fetch('STDDEF_CXX', 'c++'))
sysroot = ENV['STDDEF_SYSROOT']
run = ENV.fetch('STDDEF_RUN', sysroot ? '0' : '1') == '1'
flags = Shellwords.split(ENV.fetch('STDDEF_FLAGS', '-O1 -g -Wall -Wextra -Werror -Wsystem-headers'))
flags += ['--sysroot=' + sysroot, '-I' + root + '/sys', '-DEXPECT_AMD64'] if sysroot
Dir.mktmpdir('ember-stddef-') do |work|
  inc = work + '/include'
  FileUtils.mkdir_p(inc + '/sys'); FileUtils.mkdir_p(inc + '/machine')
  FileUtils.cp(root + '/sys/sys/stddef.h', inc + '/sys/stddef.h')
  if sysroot
    FileUtils.rmdir(inc + '/machine')
    %w[amd64 x86 i386].each { |arch| File.symlink(root + '/sys/arch/' + arch + '/include', inc + '/' + arch) }
    File.symlink(root + '/sys/arch/amd64/include', inc + '/machine')
  else
    # Native prerequisites model only type/feature plumbing. Target mode uses real OS headers.
    File.write(inc + '/sys/cdefs.h', "#define __GNUC_PREREQ__(a,b) (__GNUC__ > (a) || (__GNUC__ == (a) && __GNUC_MINOR__ >= (b)))\n#define __unreachable() __builtin_unreachable()\n")
    File.write(inc + '/sys/featuretest.h', "#ifndef _ANSI_SOURCE\n#define _NETBSD_SOURCE 1\n#endif\n")
    FileUtils.cp(root + '/sys/sys/null.h', inc + '/sys/null.h')
    File.write(inc + '/machine/ansi.h', "#ifndef STDDEF_MODEL_ANSI\n#define STDDEF_MODEL_ANSI\ntypedef __SIZE_TYPE__ size_t;\ntypedef __PTRDIFF_TYPE__ ptrdiff_t;\n#ifndef __cplusplus\ntypedef __WCHAR_TYPE__ wchar_t;\n#endif\n#endif\n")
  end
  standards = sysroot ? %w[c11 c23 c++20 c99] : %w[c11 c23 c++20]
  cases = standards.flat_map { |std| %w[sys-first compiler-first partial-first sys-only own-symlink].map { |kind| [std, kind, []] } }
  %w[_KERNEL _STANDALONE __lint__].each do |mode|
    %w[c11 c23].each { |std| cases << [std, mode, ['-D' + mode, '-nostdinc']] }
  end
  cases.each do |std, kind, extra|
    provider = kind != 'own-symlink' && extra.empty?
    headers = case kind
    when 'sys-first' then "#include <sys/stddef.h>\n#include <stddef.h>\n"
    when 'compiler-first' then "#include <stddef.h>\n#include <sys/stddef.h>\n"
    when 'partial-first' then "#define __need_size_t\n#include <stddef.h>\n#if defined(_GCC_MAX_ALIGN_T) || defined(__CLANG_MAX_ALIGN_T_DEFINED)\n#error partial request exposed max_align_t\n#endif\n#include <sys/stddef.h>\n#include <stddef.h>\n"
    when 'own-symlink' then "#include <stddef.h>\n#include <sys/stddef.h>\n"
    else "#include <sys/stddef.h>\n"
    end
    File.symlink('sys/stddef.h', inc + '/stddef.h') if kind == 'own-symlink'
    source = work + (std.start_with?('c++') ? '/test.cc' : '/test.c')
    File.write(source, headers + File.read(__dir__ + '/stddef-interop-fixture.c'))
    cmd = (std.start_with?('c++') ? cxx : cc) + ['-I' + inc] + flags + extra + ['-std=' + std, '-DEXPECT_PROVIDER=' + (provider ? '1' : '0'), source, '-o', work + '/test']
    cmd.insert(-3, '-c') unless run || flags.include?('--analyze')
    out, err, status = Open3.capture3(*cmd)
    abort "#{std}/#{kind}:\n" + out + err unless status.success?
    if run
      out, err, status = Open3.capture3(work + '/test')
      abort "#{std}/#{kind}:\n" + out + err unless status.success?
    end
    File.unlink(inc + '/stddef.h') if kind == 'own-symlink'
    puts "#{std}/#{kind}: PASS"
  end
  puts(sysroot ? 'amd64 real-header compile contracts; no target execution' : "Native type/layout #{run ? 'execution' : 'compilation/analysis'}; OS prerequisite plumbing modeled")
end
