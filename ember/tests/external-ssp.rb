#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted real make and GCC stack-protector checks.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
abort 'Usage: external-ssp.rb NBMAKE_WRAPPER FULL_SOURCE GCC_PREFIX [KERNEL_DIAGNOSTIC_LOG NEW_OUTPUT]' unless [3, 5].include?(ARGV.size)
make, src, gcc = ARGV.first(3).map { |p| File.realpath(p) }
cc = gcc + '/bin/x86_64--netbsd-gcc'
def capture(*args, **options)
  Open3.capture3(*args, **options)
end
def checked(*args, **options)
  out, err, status = capture(*args, **options)
  abort "Command failed: #{out}#{err}" unless status.success?
  out
end
abort 'Require the GCC16 amd64 cross compiler' unless checked(cc, '-dumpfullversion').strip == '16.2.0' && checked(cc, '-dumpmachine').strip == 'x86_64--netbsd'
run = lambda do |work|
  fixture = work + '/flags.mk'
  File.write(fixture, <<~MAKE)
    .if ${TEST_INTERNAL:Uno} == "yes"
    .undef EXTERNAL_TOOLCHAIN
    .else
    EXTERNAL_TOOLCHAIN=${SSP_GCC_PREFIX}
    .endif
    .include "#{src}/share/mk/bsd.own.mk"
    .include "#{src}/share/mk/bsd.sys.mk"
    .include "#{src}/sys/conf/ssp.mk"
  MAKE
  # Use the real make binary with explicit target settings. A generated
  # wrapper can retain EXTERNAL_TOOLCHAIN in its environment after .undef.
  base = [{'EXTERNAL_TOOLCHAIN' => nil, 'MAKEFLAGS' => nil, 'MFLAGS' => nil},
    File.dirname(make) + '/nbmake', '-C', work, '-m', src + '/share/mk', '-f', fixture,
    'NETBSDSRCDIR=' + src, 'SSP_GCC_PREFIX=' + gcc, 'TOOLDIR=' + File.dirname(File.dirname(make)),
    'EMBER_COMPILER_FAMILY=gcc', 'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'USETOOLS=yes', 'MAKECONF=/dev/null', 'USE_SSP=yes', 'MKREPRO=no']
  query = lambda do |*extra|
    checked(*base, *extra, '-v', 'COPTS').split
  end
  external = query.call
  abort 'External GCC did not select strong SSP' unless external.include?('-fstack-protector-strong') && !external.include?('-fstack-protector')
  abort 'SSP diagnostic was removed' unless external.include?('-Wstack-protector') && !external.include?('-Wno-error=stack-protector')
  internal = query.call('TEST_INTERNAL=yes')
  abort 'In-tree compiler SSP policy changed' unless internal.include?('-fstack-protector') && !internal.include?('-fstack-protector-strong')
  clang = query.call('ACTIVE_CC=clang')
  abort 'External Clang SSP policy changed' unless clang.include?('-fstack-protector') && !clang.include?('-fstack-protector-strong')
  %w[NOSSP=yes USE_SSP=no BINDIR=/usr/mdec COVERITY_TOP_CONFIG=yes].each do |option|
    abort 'An SSP exclusion was lost: ' + option if query.call(option).any? { |arg| arg.start_with?('-fstack-protector') }
  end
  %w[HOSTPROG=host HOSTLIB=host].each do |option|
    abort 'Target SSP policy polluted host compilation' if query.call(option).include?('-fstack-protector-strong')
  end
  %w[kern_ssp.c cpu.c subr_kcov.c].each do |name|
    flag = checked(*base, '-v', 'COPTS.' + name)
    abort 'Kernel guard initialization exception was lost: ' + name unless flag.include?('-fno-stack-protector')
  end
  source = src + '/ember/tests/fixtures/ssp-gcc16.c'
  common = [cc, '-O2', '-std=gnu11', '-ffreestanding', '-fno-pie', '-Wstack-protector', '--param', 'ssp-buffer-size=1', '-Werror']
  out, err, status = capture(*common, '-fstack-protector', '-S', source, '-o', work + '/rejected.s')
  File.write(work + '/basic-zero.log', out + err)
  abort 'Reduced zero-array case did not reproduce the GCC16 diagnostic' unless !status.success? && err.include?('all local arrays are less than 1 bytes long')
  [['basic', ['-fstack-protector', '-DOMIT_ZERO_ARRAY']], ['strong', ['-fstack-protector-strong']], ['disabled', ['-fstack-protector-strong', '-fno-stack-protector']]].each do |name, flags|
    checked(*common, *flags, '-S', source, '-o', work + '/' + name + '.s')
  end
  guarded = lambda do |name, function|
    assembly = File.read(work + '/' + name + '.s')
    body = assembly.split(/^#{function}:\s*$/)[1]&.split(/^\s*\.size\s+#{function},/)&.first
    abort 'Missing assembly function ' + function unless body
    body.include?('__stack_chk_fail') && body.scan('__stack_chk_guard').size >= 2
  end
  %w[ssp_character_one ssp_character_large].each do |function|
    abort 'Existing character-array protection was lost' unless guarded.call('basic', function) && guarded.call('strong', function)
  end
  abort 'Address-taken scalar did not gain protection' if guarded.call('basic', 'ssp_address_local') || !guarded.call('strong', 'ssp_address_local')
  abort 'Nested zero-array aggregate lacks a strong guard' unless guarded.call('strong', 'ssp_nested_zero')
  abort 'Plain scalar unexpectedly requires a guard' if guarded.call('strong', 'ssp_plain_local')
  abort 'Per-file no-SSP override did not win' if File.read(work + '/disabled.s').include?('__stack_chk_fail')
  checked(*common, '-fstack-protector-strong', '-fanalyzer', '-c', source, '-o', work + '/analyzer.o')
  if ENV['CPPCHECK']
    checked(ENV['CPPCHECK'], '--enable=warning,performance,portability', '--error-exitcode=1', '--platform=unix64', '--std=c11', source)
  end
  puts 'PASS: real make SSP selection/exclusions and GCC16 assembly guard coverage'
  if ARGV.size == 5
    log = File.realpath(ARGV[3])
    %w[bcsp.c netbsd32_signal.c].each do |name|
      line = File.foreach(log).find { |text| text.start_with?(cc + ' ') && text.include?(' -c ') && text.include?('/' + name) }
      abort 'Actual compiler command missing: ' + name unless line
      args = Shellwords.split(line)
      abort 'Expected the original basic SSP and Werror' unless args.include?('-fstack-protector') && args.include?('-Werror')
      args[args.index('-o') + 1] = work + '/' + name + '.o'
      # The caller supplies the configured kernel directory explicitly.
      cwd = ENV.fetch('SSP_KERNEL_DIR')
      out, err, status = capture(*args, chdir: cwd)
      File.write(work + '/' + name + '.before.log', out + err)
      abort 'Actual TU did not reproduce its SSP error: ' + name unless !status.success? && err.include?('all local arrays are less than 1 bytes long')
      args[args.index('-fstack-protector')] = external.find { |arg| arg == '-fstack-protector-strong' }
      out, err, status = capture(*args, chdir: cwd)
      File.write(work + '/' + name + '.after.log', out + err)
      File.write(work + '/' + name + '.command.json', JSON.pretty_generate(args))
      abort 'Actual strong-SSP TU compile failed: ' + name + ': ' + err unless status.success?
    end
    puts 'PASS: actual GENERIC bcsp/netbsd32_signal basic failures and strong-SSP Werror compiles'
  end
end
if ARGV.size == 5
  output = File.expand_path(ARGV[4])
  abort 'NEW_OUTPUT exists' if File.exist?(output)
  FileUtils.mkdir_p(output)
  run.call(output)
else
  Dir.mktmpdir('ember-external-ssp-', '/tmp') { |work| run.call(File.realpath(work)) }
end
