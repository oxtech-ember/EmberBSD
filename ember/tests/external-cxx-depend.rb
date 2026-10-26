# SPDX-License-Identifier: BSD-2-Clause
# Exercise the real BSD compile/depend rules with the selected target headers.
require 'open3'
require 'fileutils'
require 'json'
require 'digest'
require 'shellwords'

make, src, toolchain, runtime, sysroot, work = ARGV
abort 'Usage: ruby external-cxx-depend.rb NBMAKE SOURCE GCC_PREFIX RUNTIME SYSROOT NEW_RECEIPT' unless work
make, src, toolchain, runtime, sysroot = [make, src, toolchain, runtime, sysroot].map { |p| File.realpath(p) }
work = File.expand_path(work)
abort 'Receipt already exists' if File.exist?(work)
FileUtils.mkdir_p(work)
cc = toolchain + '/bin/x86_64--netbsd-gcc'
cxx = toolchain + '/bin/x86_64--netbsd-g++'
capture = lambda do |name, *argv|
  File.write(work + '/' + name + '.argv.json', JSON.pretty_generate(argv) + "\n")
  out, err, status = Open3.capture3({ 'MAKEFLAGS' => nil, 'MAKEOBJDIR' => nil, 'MAKEOBJDIRPREFIX' => nil,
    'EMBER_EXTERNAL_GCC_RUNTIME' => nil }, *argv)
  File.write(work + '/' + name + '.log', out + err)
  [out, err, status]
end
%w[gcc g++].each do |name|
  driver = toolchain + '/bin/x86_64--netbsd-' + name
  out, err, status = capture.call(name + '-version', driver, '-dumpfullversion')
  abort err unless status.success? && out.strip == '16.2.0'
  out, err, status = capture.call(name + '-target', driver, '-dumpmachine')
  abort err unless status.success? && out.strip == 'x86_64--netbsd'
end
File.write(work + '/probe.cpp', "#include <cassert>\n#include <cstring>\nint cpp_probe() { assert(std::strlen(\"probe\") == 5); return 0; }\n")
File.write(work + '/cprobe.c', "#include <string.h>\nint c_probe(void);\nint c_probe(void) { return (int)strlen(\"probe\"); }\n")
setup = ".include \"#{src}/share/mk/bsd.own.mk\"\n.include \"#{src}/share/mk/bsd.gcc.mk\"\n"
File.write(work + '/Makefile', setup + "LIB=dependprobe\nLIBISCXX=yes\nSRCS=probe.cpp cprobe.c\nWARNS=4\nMAN=\n.include <bsd.lib.mk>\n")
File.write(work + '/query.mk', setup)
args = [make, '-C', work, '-m', src + '/share/mk', 'NETBSDSRCDIR=' + src,
  'NETBSDOBJDIR=' + work, 'MAKEOBJDIR=' + work, 'TOOLDIR=' + File.dirname(File.dirname(make)),
  'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'MACHINE_GNU_PLATFORM=x86_64--netbsd',
  'EMBER_COMPILER_FAMILY=gcc', 'USETOOLS=yes', 'TOOLCHAIN_MISSING=yes', 'EXTERNAL_TOOLCHAIN=' + toolchain,
  'DESTDIR=' + sysroot, 'MKREPRO=no', 'MKCOMPAT=no', 'MKLLVM=no', 'MKLIBCXX=no',
  'MKCXX=yes', 'MKCTF=no', 'MKLINT=no', 'MKDEBUG=no', 'MKSHARE=no']
selected = ['EMBER_EXTERNAL_GCC_RUNTIME=' + runtime]
query_number = 0
query = lambda do |extra, flag, fixture = nil|
  fixture ||= work + '/query.mk'
  query_number += 1
  out, err, status = capture.call('query-' + query_number.to_s + '-' + flag.gsub(/\W/, '_'),
    *args, '-f', fixture, *extra, '-v', flag)
  abort out + err unless status.success?
  out.strip
end
metadata = {}
%w[CXXFLAGS CPPFLAGS CFLAGS MKDEPCXXFLAGS].each { |v| metadata[v] = query.call(selected, v) }
File.write(work + '/flags.json', JSON.pretty_generate(metadata) + "\n")
abort 'C++ headers leaked into C CPPFLAGS' if metadata.fetch('CPPFLAGS').include?('/include/c++')
abort 'C++ headers leaked into CFLAGS' if metadata.fetch('CFLAGS').include?('/include/c++')
%w[HOSTPROG HOSTLIB].each do |host|
  %w[CPPFLAGS CXXFLAGS MKDEPCXXFLAGS].each do |v|
    abort "Target headers leaked into #{host} #{v}" if query.call(selected + [host + '=probe'], v).include?(runtime)
  end
end
%w[CPPFLAGS CXXFLAGS MKDEPCXXFLAGS].each do |v|
  abort "Default target flags changed: #{v}" if query.call([], v).include?(runtime)
end
if (baseline = ENV['CXX_DEPEND_BASELINE_GCC'])
  baseline = File.realpath(baseline)
  oldquery = work + '/baseline-query.mk'
  File.write(oldquery, ".include \"#{src}/share/mk/bsd.own.mk\"\n.include \"#{baseline}\"\n")
  [[], selected + ['HOSTPROG=probe'], selected + ['HOSTLIB=probe']].each do |mode|
    %w[CPPFLAGS CXXFLAGS CFLAGS MKDEPCXXFLAGS].each do |v|
      abort "Unselected/host #{v} differs from the original adapter" unless
        query.call(mode, v) == query.call(mode, v, oldquery)
    end
  end
  File.write(work + '/baseline.sha256', Digest::SHA256.file(baseline).hexdigest + '  ' + baseline + "\n")
  puts 'PASS exact original/default and HOSTPROG/HOSTLIB flag comparisons'
end
run = lambda do |name, *targets|
  out, err, status = capture.call(name, *args, *selected, *targets)
  abort out + err unless status.success?
end
run.call('probe-compile', 'probe.o')
puts 'PASS real C++ compile with the audited header provider'
run.call('probe-depend', 'probe.d')
puts 'PASS real .cpp to .d dependency generation'
dep = File.read(work + '/probe.d')
abort 'Dependency selected a foreign C++ provider' if dep.lines.grep(%r{/include/c\+\+/}).any? { |line| !line.include?(runtime + '/include/c++/') }
%w[cassert cstring].each do |header|
  abort "Dependency omitted staged #{header}" unless dep.include?(runtime + '/include/c++/' + header)
end
# Compare the compiler's header trace with the dependency provider.
out, err, status = capture.call('probe-header-trace', cxx, *Shellwords.split(metadata.fetch('CXXFLAGS')),
  *Shellwords.split(metadata.fetch('CPPFLAGS')), '-H', '-fsyntax-only', work + '/probe.cpp')
abort out + err unless status.success?
abort 'Compile selected a foreign C++ provider' if err.lines.grep(%r{/include/c\+\+/}).any? { |line| !line.include?(runtime + '/include/c++/') }
%w[cassert cstring].each do |header|
  abort "Compile trace omitted staged #{header}" unless err.include?(runtime + '/include/c++/' + header)
end
run.call('c-probe', 'cprobe.o', 'cprobe.d')
abort 'C dependency selected C++ headers' if File.read(work + '/cprobe.d').include?('/include/c++')
atf = src + '/external/bsd/atf'
atfwork = work + '/atf'
FileUtils.mkdir_p(atfwork)
atfargs = args.dup
atfargs[atfargs.index('-C') + 1] = atf + '/lib/libatf-c++'
atfargs[atfargs.index { |a| a.start_with?('MAKEOBJDIR=') }] = 'MAKEOBJDIR=' + atfwork
out, err, status = capture.call('atf-compile-depend', *atfargs, *selected, 'check.o', 'check.d')
abort out + err unless status.success?
abort 'ATF dependency omitted staged cstring' unless File.read(atfwork + '/check.d').include?(runtime + '/include/c++/cstring')
negative = work + '/atf-negative'
FileUtils.mkdir_p(negative)
badargs = atfargs.dup
badargs[badargs.index { |a| a.start_with?('MAKEOBJDIR=') }] = 'MAKEOBJDIR=' + negative
out, err, status = capture.call('atf-negative', *badargs, *selected, 'MKDEPCXXFLAGS=', 'check.d')
abort 'Empty C++ dependency hook unexpectedly succeeded' if status.success?
abort 'Negative control did not fail on the missing C++ header' unless (out + err).match?(/fatal error: cstring: No such file/)
File.write(work + '/source.sha256', %w[share/mk/bsd.gcc.mk share/mk/bsd.dep.mk external/bsd/atf/dist/atf-c++/check.cpp].map { |p| Digest::SHA256.file(src + '/' + p).hexdigest + '  ' + p }.join("\n") + "\n")
puts 'PASS actual ATF check.cpp compile and dependency generation, C and host isolation'
puts 'PASS actual ATF dependency rejects removal of the C++ preprocessing hook'
