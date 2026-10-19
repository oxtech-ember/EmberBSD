#!/usr/bin/env ruby
# Origin: EmberBSD actual-source firmware framebuffer contracts.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'shellwords'
require 'rbconfig'

root = File.expand_path('../..', __dir__)
source = ARGV[0] || root
c = File.read(source + '/sys/dev/fdt/simplefb.c')
baseline = !c.include?('simplefb_console_get_framebuffer')
# Current ownership uses the one MI record. Keep old-export baseline controls.
if c.include?('wsdisplay_bootfb_legacy_device')
  env = { 'BOOTFB_LEGACY_ONLY' => '1' }
  env['BOOTFB_SKIP_MUTANTS'] = '1' if ENV['SIMPLEFB_SKIP_MUTANTS'] == '1'
  env['BOOTFB_EVIDENCE'] = ENV['SIMPLEFB_CAPTURE'] if ENV['SIMPLEFB_CAPTURE']
  exec(env, RbConfig.ruby, root + '/ember/tools/bootfb-issuer-contract.rb', source)
end
Dir.mktmpdir('simplefb-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  FileUtils.mkdir_p(work + '/sys')
  File.write(work + '/sys/bus.h', <<~HEADER)
    #ifndef SIMPLEFB_TEST_BUS_H
    #define SIMPLEFB_TEST_BUS_H
    #include <stdint.h>
    typedef void *bus_space_tag_t;
    typedef uint64_t bus_addr_t, bus_size_t;
    typedef uintptr_t bus_space_handle_t;
    #endif
  HEADER
  File.write(work + '/sys/device.h', <<~HEADER)
    #ifndef SIMPLEFB_TEST_DEVICE_H
    #define SIMPLEFB_TEST_DEVICE_H
    struct device;
    typedef struct device *device_t;
    #endif
  HEADER
  FileUtils.cp(source + '/sys/dev/fdt/simplefbvar.h', work + '/simplefbvar.h')
  structure = c[/^struct simplefb_softc \{.*?^};/m]
  abort 'missing production structure' unless structure
  File.write(work + '/simplefb-structure.h', structure)
  names = %w[simplefb_read_framebuffer simplefb_console_reserve
             simplefb_console_get_framebuffer simplefb_console_release
             simplefb_console_commit simplefb_console_fallback
             simplefb_attach_genfb simplefb_attach]
  if baseline
    names -= %w[simplefb_read_framebuffer simplefb_console_get_framebuffer
                simplefb_console_release]
  end
  body = names.map do |name|
    match = c.match(/^(?:static )?(?:int|bool|void)\n#{name}\(.*?^}/m)
    abort 'missing production function ' + name unless match
    match[0]
  end.join("\n\n")
  File.write(work + '/simplefb-body.h', body + "\n")
  cc = Shellwords.split(ENV.fetch('CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  common = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-Wno-unused-parameter', *flags]
  variants = baseline ? ['baseline'] : %w[direct deferred multicons]
  variants.each do |variant|
    defs = []
    defs << '-DVIRTGPU_CONSOLE' unless variant == 'direct'
    defs << '-DWSDISPLAY_MULTICONS' if variant == 'multicons'
    defs << '-DSIMPLEFB_BASELINE' if baseline
    cmd = [*common, *defs, '-I' + work,
           root + '/ember/tools/simplefb-contract.c', '-o', work + '/test']
    abort 'contract compile failed' unless system(*cmd)
    if baseline
      %w[high-address wide-stride zero-width short-stride span-overflow
         address-wrap null-owner].each do |test|
        abort 'baseline survived ' + test if system(work + '/test', test)
        puts 'Baseline rejected: ' + test
      end
    else
      abort 'contract failed: ' + variant unless system(work + '/test')
    end
    if ENV['SIMPLEFB_CAPTURE']
      to = ENV['SIMPLEFB_CAPTURE'] + '/' + variant
      FileUtils.mkdir_p(to)
      FileUtils.mkdir_p(to + '/sys')
      FileUtils.cp_r(work + '/sys/.', to + '/sys')
      %w[simplefbvar.h simplefb-structure.h simplefb-body.h].each do |name|
        FileUtils.cp(work + '/' + name, to + '/' + name)
      end
      File.write(to + '/command.txt', Shellwords.join(cmd) + "\n")
    end
  end
  unless baseline || ENV['SIMPLEFB_SKIP_MUTANTS'] == '1'
    mutants = [
      ['foreign-query', 'simplefb_console_get_framebuffer', 'simplefb_owner != owner', 'false'],
      ['foreign-release', 'simplefb_console_release', 'simplefb_owner != owner', 'false'],
      ['committed-release', 'simplefb_console_release', 'simplefb_committed', 'false'],
      ['foreign-commit', 'simplefb_console_commit', 'simplefb_owner != owner', 'false'],
      ['repeat-fallback', 'simplefb_console_fallback', 'simplefb_deferred = NULL;', '(void)0;'],
      ['collapsed-format', 'simplefb_read_framebuffer', 'SIMPLEFB_FORMAT_A8B8G8R8', 'SIMPLEFB_FORMAT_X8R8G8B8'],
      ['invalid-query', 'simplefb_console_get_framebuffer', 'simplefb_deferred->sc_framebuffer_error != 0', 'false']
    ]
    mutants.each do |label, name, before, after|
      function = body[/^(?:static )?(?:int|bool|void)\n#{name}\(.*?^}/m]
      abort 'mutant function absent' unless function && function.include?(before)
      changed = function.sub(before, after)
      File.write(work + '/simplefb-body.h', body.sub(function, changed))
      cmd = [*common, '-DVIRTGPU_CONSOLE', '-I' + work,
             root + '/ember/tools/simplefb-contract.c', '-o', work + '/test']
      abort 'mutant did not compile: ' + label unless system(*cmd)
      passed = File.open(work + '/mutant.log', 'w') do |log|
        system(work + '/test', out: log, err: log)
      end
      abort 'surviving mutant: ' + label if passed
      puts 'Mutant rejected: ' + label
    end
  end
end
