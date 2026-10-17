# SPDX-License-Identifier: BSD-2-Clause
# Test the production sys header with compiler resources and resource-free fallback.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
cc = Shellwords.split(ENV.fetch('STDARG_CC', 'cc'))
cxx = Shellwords.split(ENV.fetch('STDARG_CXX', 'c++'))
sysroot = ENV['STDARG_SYSROOT']
run = ENV.fetch('STDARG_RUN', sysroot ? '0' : '1') == '1'
flags = Shellwords.split(ENV.fetch('STDARG_FLAGS', '-O1 -g -Wall -Wextra -Werror -Wsystem-headers'))
flags += ['--sysroot=' + sysroot, '-I' + root + '/sys'] if sysroot
Dir.mktmpdir('ember-stdarg-') do |work|
  inc = work + '/include'
  FileUtils.mkdir_p(inc + '/sys')
  FileUtils.cp(root + '/sys/sys/stdarg.h', inc + '/sys/stdarg.h')
  if sysroot
    File.symlink(root + '/sys/arch/amd64/include', inc + '/machine')
    File.symlink(root + '/sys/arch/x86/include', inc + '/x86')
  end
  unless sysroot
    # Native host models for the three OS prerequisites. Target mode uses real headers.
    File.write(inc + '/sys/cdefs.h', <<~C)
      #define __GNUC_PREREQ__(a, b) (__GNUC__ > (a) || (__GNUC__ == (a) && __GNUC_MINOR__ >= (b)))
      #define __nothing ((void)0)
    C
    File.write(inc + '/sys/ansi.h', "#ifdef __lint__\ntypedef char *__va_list;\n#else\ntypedef __builtin_va_list __va_list;\n#endif\n")
    File.write(inc + '/sys/featuretest.h', "#if !defined(_ANSI_SOURCE)\n#define _NETBSD_SOURCE 1\n#endif\n")
  end
  cases = %w[c11 c23 c++20].flat_map do |std|
    %w[sys-first compiler-first partial-first sys-only own-symlink].map { |kind| [std, kind, []] }
  end
  %w[_KERNEL _STANDALONE __lint__].each do |mode|
    cases << ['c11', mode, ['-D' + mode, '-nostdinc']]
    cases << ['c11', mode + '-ansi', ['-D' + mode, '-D_ANSI_SOURCE', '-DEXPECT_NO_COPY', '-nostdinc']]
  end
  cases.each do |std, kind, extra|
    fallback = kind == 'own-symlink' || extra.any?
    headers = case kind
    when 'sys-first' then "#include <sys/stdarg.h>\n#include <stdarg.h>\n"
    when 'compiler-first' then "#include <stdarg.h>\n#include <sys/stdarg.h>\n"
    when 'partial-first' then "#define __need___va_list\n#include <stdarg.h>\n#ifdef va_start\n#error partial include exposed va_start\n#endif\n#include <sys/stdarg.h>\n#include <stdarg.h>\n"
    when 'own-symlink' then "#include <stdarg.h>\n#include <sys/stdarg.h>\n"
    else "#include <sys/stdarg.h>\n"
    end
    if kind == 'own-symlink'
      File.symlink('sys/stdarg.h', inc + '/stdarg.h')
    end
    # C23 tests the compiler's one-argument form. The resource-free BSD fallback retains its two-argument form.
    start = std == 'c23' && !fallback ? 'va_start(ap);' : 'va_start(ap, count);'
    code = headers + File.read(__dir__ + '/stdarg-interop-fixture.c').sub('@START@', start)
    suffix = std.start_with?('c++') ? '.cc' : '.c'
    source = work + '/test' + suffix
    File.write(source, code)
    cmd = (suffix == '.cc' ? cxx : cc) + ['-I' + inc] + flags + ['-std=' + std] + extra
    # -nostdinc fallback uses only source OS headers and machine links above.
    cmd += [source, '-o', work + '/test']
    cmd.insert(-3, '-c') unless run && !kind.start_with?('__lint__')
    out, err, status = Open3.capture3(*cmd)
    abort "#{std}/#{kind}:\n" + out + err unless status.success?
    if run && !kind.start_with?('__lint__')
      out, err, status = Open3.capture3(work + '/test')
      abort "#{std}/#{kind}:\n" + out + err unless status.success?
    end
    File.unlink(inc + '/stdarg.h') if kind == 'own-symlink'
    puts "#{std}/#{kind}: PASS"
  end
  puts(sysroot ? 'Real target headers; compile-only unless STDARG_RUN=1' : 'Native variadic execution; OS prerequisite types/features modeled')
end
