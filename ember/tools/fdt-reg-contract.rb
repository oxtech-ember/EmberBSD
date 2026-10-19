#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

source = File.realpath(ARGV.fetch(0))
input = ENV.fetch('FDT_REG_INPUT', source + '/sys/dev/fdt/fdt_subr.c')
body = File.read(input)[/^int\nfdtbus_get_reg\(.*?^}\n/m]
abort 'Cannot find the complete actual fdtbus_get_reg function' unless body
fixture = source + '/ember/tools/fdt-reg-contract.c'
work = Dir.mktmpdir('fdt-reg-contract-')
warn "FDT range temporary directory: #{work}"
commands = []
begin
  File.write(work + '/fdt-reg-actual.h', body)
  [32, 64].each do |bits|
    output = work + "/contract#{bits}"
    argv = [ENV.fetch('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror'] +
      Shellwords.split(ENV.fetch('CFLAGS', '')) + ["-DFDT_REG_BITS=#{bits}",
      '-I' + work, fixture, '-o', output]
    stdout, stderr, status = Open3.capture3(*argv)
    commands << { argv: argv, status: status.exitstatus, stdout: stdout, stderr: stderr }
    abort stderr unless status.success?
    unless ENV.fetch('FDT_REG_COMPILE_ONLY', '0') == '1'
      argv = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [output]
      stdout, stderr, status = Open3.capture3(*argv)
      commands << { argv: argv, status: status.exitstatus, stdout: stdout, stderr: stderr }
      print stdout
      warn stderr unless stderr.empty?
      abort "FDT range contract failed with status #{status.exitstatus}" unless status.success?
    end
    if ENV['FDT_REG_OUTPUT']
      FileUtils.cp(output, ENV.fetch('FDT_REG_OUTPUT') + "#{bits}")
    end
  end
ensure
  if ENV['FDT_REG_EVIDENCE']
    evidence = ENV.fetch('FDT_REG_EVIDENCE')
    FileUtils.mkdir_p(evidence)
    FileUtils.cp(work + '/fdt-reg-actual.h', evidence) if File.file?(work + '/fdt-reg-actual.h')
    File.write(evidence + '/receipt.json', JSON.pretty_generate({
      input: input, input_sha256: Digest::SHA256.file(input).hexdigest,
      extracted_sha256: Digest::SHA256.hexdigest(body),
      fixture_sha256: Digest::SHA256.file(fixture).hexdigest,
      runner_sha256: Digest::SHA256.file(__FILE__).hexdigest,
      commands: commands,
      scope: 'Complete actual fdtbus_get_reg body. The lower uint64 decoder is controlled. Fixed-width bus typedefs model the 32-bit and 64-bit API. This is not a complete FDT parser or hardware test.'
    }) + "\n")
  end
  FileUtils.remove_entry(work)
end
