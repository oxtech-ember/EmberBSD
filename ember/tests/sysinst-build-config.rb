#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; real incremental sysinst objects and message generators.
require 'fileutils'
require 'json'
require 'digest'
require 'open3'
abort 'Usage: sysinst-build-config.rb SOURCE CONFIGURED_NBMAKE GCC_RUNTIME_OR_- NEW_OUTPUT' unless ARGV.size == 4
src, original = ARGV.first(2).map { |p| File.realpath(p) }
runtime = File.realpath(ARGV[2]) unless ARGV[2] == '-'
out = File.expand_path(ARGV[3])
abort 'Output must not exist' if File.exist?(out)
FileUtils.mkdir_p(out)
out = File.realpath(out)
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
machine = checked(original, '-V', 'MACHINE')
family = checked(original, '-V', 'EMBER_COMPILER_FAMILY')
abort 'A GCC wrapper requires its external runtime' if family == 'gcc' && !runtime
abort 'Pass - for the Clang base runtime' if family == 'clang' && runtime
make = out + '/nbmake-' + machine
File.write(make, wrapper)
File.chmod(0755, make)
dir = src + '/usr.sbin/sysinst/arch/' + machine
obj = out + '/obj/usr.sbin/sysinst/arch/' + machine
FileUtils.mkdir_p(obj)
abort 'Object isolation failed' unless checked(make, '-C', dir, '-V', '.OBJDIR') == obj
catalogs = %w[sysinstmsgs.de sysinstmsgs.es sysinstmsgs.fr sysinstmsgs.pl]
messages = %w[msg.def msg_defs.c sysinstmsgs.fmtcnt] + catalogs
objects = %w[wizard.o main.o util.o txtwalk.o menu_defs.o]
files = objects + messages + %w[menus.def menu_defs.c]
targets = objects + ['filesbuild']
identity = ['wizard.o'] + messages
identity_with_url = identity.dup
uses_id_in_url = machine != 'amd64' && checked(make, '-C', dir, '-V', '${SYSINST_FTP_DIR:Dset}').empty?
identity_with_url << 'main.o' if uses_id_in_url
runtime_objects = %w[wizard.o util.o]
results = []
previous = nil
run = lambda do |name, version, id, external, rebuild, download_dir = nil|
  # BSD make compares timestamps. Separate independent invocations across
  # one timestamp tick, including the repeat case whose mtimes must not move.
  sleep 1.1 if previous
  args = [make, '-C', dir, '-j2', 'EMBER_VERSION=' + version, 'EMBER_BUILD_ID=' + id]
  args << 'EMBER_EXTERNAL_GCC_RUNTIME=' + runtime if external
  args << 'SYSINST_FTP_DIR=' + download_dir if download_dir
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
      expected = rebuild.include?(file)
      abort "#{name}: #{file} #{expected ? 'was stale' : 'rebuilt without input change'}" unless changed == expected
    end
  end
  wizard = File.binread(obj + '/wizard.o')
  abort name + ': object has stale build ID' unless wizard.include?('Build        ' + id) || wizard.include?('Build: ' + id)
  abort name + ': object has stale runtime mode' unless wizard.include?('runtime-gcc16.tar.xz') == external
  expected_dir = download_dir || ('releases/' + id if uses_id_in_url)
  if expected_dir && !File.binread(obj + '/main.o').include?(expected_dir)
    abort name + ': object has stale download directory'
  end
  message = File.read(obj + '/msg.def')
  abort name + ': message has stale build/version' unless message.include?(id) && message.include?(version)
  previous = current
end
a = 'a' * 40
b = 'b' * 40
run.call('initial', '1.0', a, false, files)
initial = previous
run.call('repeat', '1.0', a, false, [])
# A retained build may predate the per-input stamps.
File.unlink(obj + '/ember-build-id')
run.call('adopt-old-objects', '1.0', b, false, identity_with_url)
run.call('build-id-repeat', '1.0', b, false, [])
run.call('version', '2.0', b, false, identity)
run.call('custom-download', '2.0', b, false, %w[main.o wizard.o], 'test-distribution/releases')
run.call('restore-download', '2.0', b, false, %w[main.o wizard.o])
if runtime
  run.call('runtime-on', '2.0', b, true, runtime_objects)
  run.call('runtime-repeat', '2.0', b, true, [])
end
run.call('switch-back', '1.0', a, false, identity_with_url | (runtime ? runtime_objects : []))
files.each do |file|
  abort 'Switch-back changed reproducible output: ' + file unless previous[file][:sha256] == initial[file][:sha256]
end
# Recreate a missing published input even when the render is up to date.
menu_def = obj + '/menus.def'
content = File.binread(menu_def)
File.unlink(menu_def)
checked(make, '-C', dir, 'EMBER_VERSION=1.0', 'EMBER_BUILD_ID=' + a, 'menus.def')
abort 'Missing menu was not recovered' unless File.binread(menu_def) == content

# A platform can also supply a menu that embeds the release identity.
menu = out + '/release-menu'
File.write(menu, "menu release, title \"@@EMBER_VERSION@@ @@BUILD@@\";\n")
[a, b].each do |id|
  sleep 1.1
  checked(make, '-C', dir, 'MENUS_MI=' + menu, 'MENUS_MD=', 'EMBER_VERSION=2.0',
    'EMBER_BUILD_ID=' + id, 'menus.def')
  abort 'Platform menu has stale identity' unless File.read(obj + '/menus.def').include?('2.0 ' + id)
end
puts "PASS: sysinst ID/version, #{runtime ? 'base and GCC runtimes' : 'base runtime'}, unchanged repeats and reproducible switch-back"
