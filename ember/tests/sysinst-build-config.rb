#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; real incremental sysinst objects and message generators.
require 'fileutils'
require 'json'
require 'digest'
require 'open3'
abort 'Usage: sysinst-build-config.rb SOURCE CONFIGURED_NBMAKE GCC_RUNTIME NEW_OUTPUT' unless ARGV.size == 4
src, original, runtime = ARGV.first(3).map { |p| File.realpath(p) }
out = File.expand_path(ARGV[3])
abort 'Output must not exist' if File.exist?(out)
FileUtils.mkdir_p(out)
def checked(*args, **options)
  stdout, stderr, status = Open3.capture3(*args, **options)
  abort "Command failed: #{stdout}#{stderr}" unless status.success?
  stdout.strip
end
old_source = checked(original, '-V', 'NETBSDSRCDIR')
old_obj = checked(original, '-V', 'NETBSDOBJDIR')
wrapper = File.read(original).gsub(old_source, src).gsub(old_obj, out + '/obj')
# Start with the ordinary base-runtime case. Individual make invocations
# select the audited external runtime through the existing public input.
wrapper = wrapper.lines.reject { |line| line.start_with?('EMBER_EXTERNAL_GCC_RUNTIME=') }.join
File.write(out + '/nbmake-amd64', wrapper)
File.chmod(0755, out + '/nbmake-amd64')
dir = src + '/usr.sbin/sysinst/arch/amd64'
obj = out + '/obj/usr.sbin/sysinst/arch/amd64'
FileUtils.mkdir_p(obj)
abort 'Object isolation failed' unless checked(out + '/nbmake-amd64', '-C', dir, '-V', '.OBJDIR') == obj
files = %w[wizard.o main.o msg.def menus.def msg_defs.c menu_defs.c sysinstmsgs.fmtcnt sysinstmsgs.de sysinstmsgs.es sysinstmsgs.fr sysinstmsgs.pl]
targets = %w[wizard.o main.o sysinstmsgs.de sysinstmsgs.es sysinstmsgs.fr sysinstmsgs.pl]
results = []
previous = nil
run = lambda do |name, version, id, external, rebuild|
  # BSD make compares timestamps. Separate independent invocations across
  # one timestamp tick, including the repeat case whose mtimes must not move.
  sleep 1.1 if previous
  args = [out + '/nbmake-amd64', '-C', dir, '-j2',
    'EMBER_COMPILER_FAMILY=gcc', 'EMBER_VERSION=' + version, 'EMBER_BUILD_ID=' + id]
  args << 'EMBER_EXTERNAL_GCC_RUNTIME=' + runtime if external
  args += targets
  stdout, stderr, status = Open3.capture3(*args)
  File.write(out + '/' + name + '.log', stdout + stderr)
  abort "Actual sysinst build failed: #{name}: #{stderr}" unless status.success?
  current = files.to_h do |file|
    path = obj + '/' + file
    abort 'Missing generated output: ' + file unless File.file?(path)
    [file, {sha256: Digest::SHA256.file(path).hexdigest, mtime: File.mtime(path).to_f}]
  end
  results << {name: name, version: version, build_id: id, runtime: external,
    argv: args, outputs: current}
  File.write(out + '/receipt.json', JSON.pretty_generate(results) + "\n")
  if previous
    files.each do |file|
      changed = current[file][:mtime] > previous[file][:mtime]
      abort "#{name}: #{file} #{rebuild ? 'was stale' : 'rebuilt without input change'}" unless changed == rebuild
    end
  end
  wizard = File.binread(obj + '/wizard.o')
  abort name + ': object has stale build ID' unless wizard.include?('Build: ' + id)
  abort name + ': object has stale runtime mode' unless wizard.include?('runtime-gcc16.tar.xz') == external
  message = File.read(obj + '/msg.def')
  abort name + ': message has stale build/version' unless message.include?(id) && message.include?(version)
  previous = current
end
a = 'a' * 40
b = 'b' * 40
run.call('initial', '1.0', a, false, true)
initial = previous
run.call('repeat', '1.0', a, false, false)
# Adopt cached objects from a build that predates the configuration stamp.
File.unlink(obj + '/ember-build-config') if File.file?(obj + '/ember-build-config')
run.call('adopt-old-objects', '1.0', b, false, true)
run.call('build-id-repeat', '1.0', b, false, false)
run.call('version', '2.0', b, false, true)
run.call('runtime-on', '2.0', b, true, true)
run.call('runtime-repeat', '2.0', b, true, false)
run.call('switch-back', '1.0', a, false, true)
files.each do |file|
  abort 'Switch-back changed reproducible output: ' + file unless previous[file][:sha256] == initial[file][:sha256]
end
puts 'PASS: actual sysinst objects, messages and catalogs update for ID/version/runtime; repeats are stable and switch-back reproduces outputs'
