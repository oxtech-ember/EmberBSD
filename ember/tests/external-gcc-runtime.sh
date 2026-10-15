#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted external GCC runtime regression.
set -eu
exec ruby - "$@" <<'RUBY'
require 'tmpdir'
require 'fileutils'
require 'open3'
abort 'Usage: external-gcc-runtime.sh NBMAKE_WRAPPER FULL_SOURCE [--links GCC_PREFIX RUNTIME SYSROOT NEW_OUTPUT]' unless ARGV.length == 2 || (ARGV.length == 7 && ARGV[2] == '--links')
make, src = ARGV.first(2).map { |p| File.realpath(p) }
def capture(*args)
  Open3.capture3(*args)
end
def require_ok(out, err, status)
  abort "make failed: #{out}#{err}" unless status.success?
  out.strip
end
Dir.mktmpdir('ember-external-runtime-', '/tmp') do |work|
  work = File.realpath(work)
  runtime = work + '/runtime'
  crt = runtime + '/lib/gcc/x86_64--netbsd/16.2.0'
  FileUtils.mkdir_p([crt, runtime + '/include/c++/x86_64--netbsd/bits', runtime + '/include/c++/backward'])
  %w[crtbegin.o crtbeginS.o crtend.o crtendS.o libgcc.a libgcc_eh.a].each { |n| File.write(crt + '/' + n, '') }
  %w[libgcc_s.so.1 libstdc++.so.7 libstdc++.a libsupc++.a].each { |n| File.write(runtime + '/lib/' + n, '') }
  File.symlink('libstdc++.so.7', runtime + '/lib/libstdc++.so')
  File.write(runtime + '/include/c++/vector', '')
  File.write(runtime + '/include/c++/x86_64--netbsd/bits/c++config.h', '')
  prog = work + '/prog.mk'
  lib = work + '/lib.mk'
  # A configured wrapper can search an older exported share/mk first.
  # Include the adapter under test explicitly before the real build rules.
  setup = ".include \"#{src}/share/mk/bsd.own.mk\"\n.include \"#{src}/share/mk/bsd.gcc.mk\"\n"
  File.write(prog, setup + "PROG_CXX=probe\nSRCS=probe.cc\nMAN=\n.include \"#{src}/share/mk/bsd.prog.mk\"\n")
  File.write(lib, setup + "LIB=probe\nLIBISCXX=yes\nSRCS=probe.cc\nSHLIB_MAJOR=1\nMAN=\n.include \"#{src}/share/mk/bsd.lib.mk\"\n")
  args = [make, '-C', work, '-m', src + '/share/mk', 'NETBSDSRCDIR=' + src,
    'DESTDIR=' + work + '/dest', 'MKREPRO=no', 'MKCOMPAT=no', 'MKLLVM=no',
    'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'MACHINE_GNU_PLATFORM=x86_64--netbsd']
  query = lambda do |fixture, extra, name|
    require_ok(*capture(*args, '-f', fixture, *extra, '-v', name))
  end
  selected = ['EMBER_EXTERNAL_GCC_RUNTIME=' + runtime]
  check = lambda { |condition, reason| abort reason unless condition }
  { 'nostdlib' => "LDFLAGS+=-nostdlib\n",
    'standalone' => "CPPFLAGS+=-D_STANDALONE\n",
    'kernel' => "CPPFLAGS+=-D_KERNEL\n" }.each do |context, flags|
    fixture = work + '/' + context + '.mk'
    File.write(fixture, flags + setup + "PROG=probe\nSRCS=probe.c\nMAN=\n.include \"#{src}/share/mk/bsd.prog.mk\"\n")
    %w[CPPFLAGS CXXFLAGS MKDEPCXXFLAGS LDFLAGS].each do |flag|
      check.call(!query.call(fixture, selected, flag).include?(runtime),
        'Target runtime polluted ' + context + ' ' + flag)
    end
  end
  flags = query.call(prog, selected, 'LDFLAGS')
  check.call(flags.include?('-B' + crt + '/') && flags.include?('-L' + crt), 'External GCC CRT/libgcc search path missing from real program link flags')
  check.call(flags.include?('-L' + runtime + '/lib') && flags.include?('-Wl,-rpath,/usr/pkg/gcc16/lib'), 'Selected runtime or target RPATH missing')
  check.call(flags.include?('-Wl,-rpath-link,' + runtime + '/lib'), 'Build-only indirect runtime search path missing')
  check.call(query.call(lib, selected + ['LDLIBC=-nodefaultlibs'], 'LDFLAGS').include?('-L' + crt), 'The libc-style -nodefaultlibs link lost staged libgcc')
  check.call(!flags.include?('-Wl,-rpath,' + work), 'Build-host RPATH leaked')
  check.call(query.call(prog, selected, 'CXXFLAGS').include?('-nostdinc++'), 'C++ headers are not isolated')
  check.call(query.call(prog, selected, 'CXXFLAGS').include?('-isystem ' + runtime + '/include/c++/x86_64--netbsd'), 'Target C++ configuration headers missing')
  check.call(query.call(prog, selected, 'MKDEPCXXFLAGS').include?('-nostdinc++'), 'C++ dependency scanning lost isolated headers')
  check.call(query.call(prog, selected, '_GCC_CRTBEGIN') == crt + '/crtbegin.o', 'Wrong executable CRT')
  check.call(query.call(prog, selected, '_GCC_LIBGCCDIR') == crt, 'Wrong explicit libgcc directory')
  check.call(query.call(prog, selected, 'LIBSTDC++') == runtime + '/lib/libstdc++.a', 'Static C++ dependency still points into OS DESTDIR')
  check.call(query.call(lib, selected, 'SHLIB_LDSTARTFILE') == work + '/dest/usr/lib/crti.o ' + crt + '/crtbeginS.o', 'Shared library must combine OS crti with selected GCC CRT')
  check.call(query.call(lib, selected, 'SHLIB_LDENDFILE') == crt + '/crtendS.o ' + work + '/dest/usr/lib/crtn.o', 'Shared library must preserve OS crtn')
  check.call(query.call(prog, selected, 'LIBCRT0') == work + '/dest/usr/lib/crt0.o', 'OS crt0 replaced')
  check.call(query.call(prog, selected, 'HAVE_GCC') == query.call(prog, [], 'HAVE_GCC'), 'In-tree GCC selector changed')
  check.call(!query.call(prog, [], 'LDFLAGS').include?(runtime), 'Default build changed')
  %w[HOSTPROG HOSTLIB].each do |host|
    fixture = work + '/' + host + '.mk'
    File.write(fixture, "#{host}=hostprobe\n" + setup)
    %w[CPPFLAGS CXXFLAGS LDFLAGS].each do |flag|
      check.call(!query.call(fixture, selected, flag).include?(runtime), 'Target runtime polluted ' + host + ' ' + flag)
    end
  end
  rejects = [ ['EMBER_EXTERNAL_GCC_RUNTIME=relative'], selected + ['MACHINE_GNU_PLATFORM=aarch64--netbsd'],
    selected + ['MACHINE_ARCH=i386'], selected + ['MLIBDIR=i386'], selected + ['ACTIVE_CC=clang'],
    selected + ['EXTERNAL_TOOLCHAIN='], selected + ['USETOOLS=no'],
    ['EMBER_EXTERNAL_GCC_RUNTIME=' + work + '/absent'] ]
  rejects.each do |extra|
    out, err, status = capture(*args, '-f', prog, *extra, '-v', 'LDFLAGS')
    check.call(!status.success? && (out + err).include?('EMBER_EXTERNAL_GCC_RUNTIME'), 'Invalid runtime input was not rejected: ' + extra.join(' '))
  end
  FileUtils.mv(runtime + '/lib/gcc/x86_64--netbsd', runtime + '/lib/gcc/aarch64--netbsd')
  FileUtils.mv(runtime + '/include/c++/x86_64--netbsd', runtime + '/include/c++/aarch64--netbsd')
  arm = selected + %w[MACHINE=evbarm MACHINE_ARCH=aarch64 MACHINE_GNU_PLATFORM=aarch64--netbsd]
  check.call(query.call(prog, arm, '_GCC_CRTBEGINS') == runtime + '/lib/gcc/aarch64--netbsd/16.2.0/crtbeginS.o', 'Matching AArch64 runtime was rejected')
  FileUtils.mv(runtime + '/lib/gcc/aarch64--netbsd', runtime + '/lib/gcc/x86_64--netbsd')
  FileUtils.mv(runtime + '/include/c++/aarch64--netbsd', runtime + '/include/c++/x86_64--netbsd')
  FileUtils.rm(runtime + '/lib/libstdc++.so')
  _, err, status = capture(*args, '-f', prog, *selected, '-v', 'LDFLAGS')
  check.call(!status.success? && err.include?('EMBER_EXTERNAL_GCC_RUNTIME'), 'Missing shared linker alias silently selects a static C++ runtime')
  File.symlink('libstdc++.so.7', runtime + '/lib/libstdc++.so')
  FileUtils.rm(crt + '/crtendS.o')
  _, err, status = capture(*args, '-f', prog, *selected, '-v', 'LDFLAGS')
  check.call(!status.success? && err.include?('EMBER_EXTERNAL_GCC_RUNTIME'), 'Incomplete CRT was accepted')
  puts 'PASS: real BSD make program/library flags, CRT ownership, host/freestanding isolation and invalid input guards'
end
if ARGV.length == 7
  prefix, runtime, sysroot = ARGV[3, 3].map { |p| File.realpath(p) }
  cc = prefix + '/bin/x86_64--netbsd-gcc'
  abort 'Require the selected GCC16 amd64 application driver' unless
    require_ok(*capture(cc, '-dumpfullversion')) == '16.2.0' &&
    require_ok(*capture(cc, '-dumpmachine')) == 'x86_64--netbsd'
  output = File.expand_path(ARGV[6])
  abort 'NEW_OUTPUT exists' if File.exist?(output)
  FileUtils.mkdir_p(output)
  File.write(output + '/probe.c', "#include <stdio.h>\nint main(void) { return puts(\"C runtime probe\") < 0; }\n")
  File.write(output + '/thread.cc', "#include <thread>\n#include <stdexcept>\nint main() { int n = 0; std::thread t([&] { n = 1; }); t.join(); try { throw std::runtime_error(\"probe\"); } catch (const std::exception&) { return n != 1; } }\n")
  File.write(output + '/library.cc', "#include <stdexcept>\nextern \"C\" int runtime_probe(int n) { try { if (!n) throw std::runtime_error(\"probe\"); } catch (const std::exception&) { return 1; } return 0; }\n")
  File.write(output + '/indirect.c', "extern int runtime_probe(int);\nint main(void) { return runtime_probe(0) != 1; }\n")
  File.write(output + '/gccprobe.c', "unsigned __int128 gcc_runtime_divide(unsigned __int128, unsigned __int128);\nunsigned __int128 gcc_runtime_divide(unsigned __int128 a, unsigned __int128 b) { return a / b; }\n")
  setup = ".include \"#{src}/share/mk/bsd.own.mk\"\n.include \"#{src}/share/mk/bsd.gcc.mk\"\n"
  File.write(output + '/programs.mk', setup + "PROGS=cprobe indirect\nSRCS.cprobe=probe.c\nSRCS.indirect=indirect.c\nSRCS.thread=thread.cc\nPROGS_CXX=thread\nLDADD.thread=-pthread\nLDADD.indirect=-L#{output} -lruntimeprobe\nLDFLAGS.cprobe+=-Wl,-Map,cprobe.map\nLDFLAGS.thread+=-Wl,-Map,thread.map\nMAN=\n.include \"#{src}/share/mk/bsd.prog.mk\"\n")
  File.write(output + '/library.mk', setup + "LIB=runtimeprobe\nLIBISCXX=yes\nSRCS=library.cc\nSHLIB_MAJOR=1\nSHLIB_MINOR=0\nMAN=\n.include \"#{src}/share/mk/bsd.lib.mk\"\n")
  File.write(output + '/gccprobe.mk', setup + "LIB=gccprobe\nSRCS=gccprobe.c\nLDLIBC=-nodefaultlibs\nLDADD=-lgcc\nDPADD=${_GCC_LIBGCCDIR}/libgcc.a\nSHLIB_MAJOR=1\nSHLIB_MINOR=0\nMAN=\n.include \"#{src}/share/mk/bsd.lib.mk\"\n")
  args = [make, '-C', output, '-m', src + '/share/mk', 'NETBSDSRCDIR=' + src, 'DESTDIR=' + sysroot,
    'EXTERNAL_TOOLCHAIN=' + prefix, 'EMBER_EXTERNAL_GCC_RUNTIME=' + runtime, 'MKCOMPAT=no',
    'MKREPRO=no', 'MKDEBUG=no', 'MKCTF=no', 'MKLINT=no', 'MKLLVM=no', 'MKPIC=yes']
  [['library.mk', 'libruntimeprobe.so.1.0'], ['programs.mk', 'cprobe', 'thread', 'indirect'], ['gccprobe.mk', 'libgccprobe.so.1.0']].each do |targets|
    fixture = targets.shift
    out, err, status = capture(*args, '-f', output + '/' + fixture, 'depend')
    File.write(output + '/' + fixture + '.depend.log', out + err)
    require_ok(out, err, status)
    out, err, status = capture(*args, '-f', output + '/' + fixture, *targets)
    File.write(output + '/' + targets.first + '.build.log', out + err)
    require_ok(out, err, status)
  end
  %w[thread library].each do |name|
    dependencies = File.read(output + '/' + name + '.d')
    abort 'C++ dependencies lost the selected target headers' unless
      dependencies.include?(runtime + '/include/c++/') &&
      dependencies.include?(runtime + '/include/c++/x86_64--netbsd/bits/c++config.h')
  end
  %w[cprobe thread indirect libruntimeprobe.so.1.0 libgccprobe.so.1.0].each do |name|
    out, err, status = capture(prefix + '/bin/x86_64--netbsd-readelf', '-h', '-d', output + '/' + name)
    require_ok(out, err, status)
    File.write(output + '/' + name + '.elf.txt', out)
    abort 'Wrong target ELF' unless out.include?('Advanced Micro Devices X86-64')
    abort 'Host RPATH leaked' if out.lines.grep(/RPATH|RUNPATH/).any? { |line| line.include?(runtime) || line.include?(sysroot) || line.include?(output) }
    abort 'Wrong C++ runtime' if %w[thread libruntimeprobe.so.1.0].include?(name) && !out.include?('[libstdc++.so.7]')
    if name == 'indirect'
      abort 'Indirect fixture did not link the C++ DSO through C' unless out.include?('[libruntimeprobe.so.1]') && !out.include?('[libstdc++.so.7]')
    end
  end
  %w[cprobe.map thread.map libruntimeprobe.so.1.map libgccprobe.so.1.map].each do |name|
    map = File.read(output + '/' + name)
    abort 'Link map did not select staged GCC CRT' unless map.include?(runtime + '/lib/gcc/x86_64--netbsd/16.2.0/crtbegin')
    abort 'Link map lost OS crti/crtn' unless map.include?(sysroot + '/usr/lib/crti.o') && map.include?(sysroot + '/usr/lib/crtn.o')
  end
  abort 'The -nodefaultlibs link did not select staged libgcc.a' unless File.read(output + '/libgccprobe.so.1.map').include?(runtime + '/lib/gcc/x86_64--netbsd/16.2.0/libgcc.a')
  puts 'PASS: actual C, C++ thread/exception, indirect C-to-C++ DSO, dependency scanning and -nodefaultlibs libgcc cross links; target execution remains separate'
end
RUBY
