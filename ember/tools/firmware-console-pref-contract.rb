#!/usr/bin/env ruby
# Origin: EmberBSD actual firmware callback regression.
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
require 'timeout'
abort 'usage: firmware-console-pref-contract.rb EDK2_SOURCE NEW_EVIDENCE' unless ARGV.size == 2
source = File.realpath(ARGV[0])
output = File.expand_path(ARGV[1])
abort 'Evidence must be new' if File.exist?(output) || File.symlink?(output)
path = source + '/EmbeddedPkg/Drivers/ConsolePrefDxe/ConsolePrefDxe.c'
text = File.binread(path).gsub("\r\n", "\n")
body = text.scan(/^STATIC\nVOID\nEFIAPI\nOnReadyToBoot \(.*?^}/m)
abort 'Actual callback must occur once' unless body.size == 1
body = body.first
# Only annotate the unchanged unused callback declarations in the host fixture.
body = body.sub('EFI_EVENT  Event,', 'EFI_EVENT  Event __attribute__((__unused__)),')
body = body.sub('VOID       *Context', 'VOID       *Context __attribute__((__unused__))')
FileUtils.mkdir_p(output)
license = text.split('#include', 2).first
File.write(output + '/console-pref-body.h', license + body + "\n")
fixture = File.expand_path('firmware-console-pref-contract.c', __dir__)
FileUtils.cp(fixture, output + '/fixture.c')
File.write(output + '/source.json', JSON.pretty_generate({
  input: path, sha256: Digest::SHA256.file(path).hexdigest,
  fixture_sha256: Digest::SHA256.file(fixture).hexdigest,
  extraction: 'Complete actual OnReadyToBoot body. Only unused callback parameter declarations are annotated. Original notice retained.',
  scope: 'EFI variable/protocol and removal services are controlled. No native firmware ABI, table installation, tty lifetime, console handoff or board acceptance.'
}) + "\n")
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
commands = []
run = lambda do |label, argv|
  out = err = ''; result = nil
  Open3.popen3(*argv, pgroup: true) do |i, o, e, waiter|
    i.close
    readers = [Thread.new { o.read }, Thread.new { e.read }]
    begin
      Timeout.timeout(30) { result = waiter.value }
    rescue Timeout::Error
      Process.kill('KILL', -waiter.pid) rescue Errno::ESRCH
      result = waiter.value
      abort "Timeout: #{label}"
    ensure
      readers.each(&:join)
      out, err = readers.map(&:value)
    end
  end
  File.write(output + '/' + label + '.stdout', out)
  File.write(output + '/' + label + '.stderr', err)
  commands << {label: label, argv: argv, exit: result.exitstatus, signal: result.termsig}
  File.write(output + '/commands.json', JSON.pretty_generate(commands) + "\n")
  puts "#{label}: #{result.success? ? 'PASS' : 'FAIL'}"
  warn err unless err.empty?
  abort "Failed: #{label}" unless result.success?
end
Dir.mktmpdir('console-pref-', ENV.fetch('TMPDIR', '/tmp')) do |temporary|
  puts "Temporary directory: #{temporary}"
  %w[standard rp1-console rp1-trace].each do |variant|
    defines = variant == 'standard' ? [] : ['-DRP1_UART_CONSOLE']
    defines << '-DCM5_BOOT_TRACE' if variant == 'rp1-trace'
    binary = temporary + '/' + variant
    run.call('compile-' + variant, [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
      *flags, *defines, '-I' + output, fixture, '-o', binary])
    run.call('run-' + variant, [binary]) unless ENV['COMPILE_ONLY'] == '1'
  end
end
