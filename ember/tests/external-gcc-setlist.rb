# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Exercise real external-runtime set ownership.
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'timeout'

abort 'Usage: external-gcc-setlist.rb SOURCE_ROOT NATIVE_TOOLDIR NEW_OUTPUT' unless ARGV.length == 3
root, tools = ARGV.first(2).map { |path| File.realpath(path) }
work = File.expand_path(ARGV[2])
abort 'new absolute output required' unless ARGV[2].start_with?('/') && !File.exist?(work)
Dir.mkdir(work)
sets = work + '/sets'
Dir.children(root + '/distrib/sets/lists').each do |name|
  next unless File.directory?(root + '/distrib/sets/lists/' + name)
  FileUtils.mkdir_p(sets + '/lists/' + name)
  File.write(sets + '/lists/' + name + '/mi', '')
end
%w[checkflist makeflist sets.subr mkvars.mk].each do |name|
  FileUtils.cp(root + '/distrib/sets/' + name, sets + '/' + name)
end
# Keep the complete architecture list so that any renewed SDK ownership fails.
FileUtils.cp(root + '/distrib/sets/lists/base/ad.aarch64', sets + '/lists/base/ad.aarch64')
directories = %w[. ./bin ./etc ./lib ./usr ./usr/lib ./usr/mdec ./usr/sbin]
controls = %w[./bin/sh ./etc/rc ./usr/lib/crt0.o ./lib/libc.so.12]
runtime = %w[./lib/libgcc_s.so.1 ./usr/lib/libstdc++.so.9.1]
{'base/mi' => directories + ['./bin/sh'],
 'base/shl.mi' => ['./lib/libc.so.12'] + runtime,
 'etc/mi' => ['./etc/rc'], 'comp/mi' => ['./usr/lib/crt0.o']}.each do |list, paths|
  lines = File.readlines(root + '/distrib/sets/lists/' + list)
  selected = lines.select { |line| paths.include?(line.split.first) }
  abort 'missing source controls in ' + list unless selected.length == paths.length
  File.write(sets + '/lists/' + list, selected.join)
end
wrapper = work + '/make'
arguments = [tools + '/bin/nbmake', '-m', root + '/share/mk', 'NETBSDSRCDIR=' + root,
  'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'HAVE_GCC=12', 'HAVE_UEFI=no',
  'USETOOLS=no', 'MKCOMPAT=no', 'MKX11=no', 'MKKMOD=no', 'MKCXX=yes',
  'MKLIBSTDCXX=yes', 'MAKECONF=/dev/null', 'MAKEOBJDIR=' + work]
File.write(wrapper, "#!/bin/sh\nexec " + Shellwords.join(arguments) +
  " TOOLCHAIN_MISSING=\"$FIXTURE_MISSING\" \"$@\"\n")
File.chmod(0755, wrapper)
run = lambda do |env, label, command, success = true|
  output = ''
  status = nil
  Open3.popen2e(env, *command, pgroup: true, chdir: work) do |input, stream, child|
    input.close
    reader = Thread.new { stream.read }
    begin
      status = Timeout.timeout(30) { child.value }
    rescue Timeout::Error
      Process.kill('KILL', -child.pid) rescue Errno::ESRCH
      child.value
    ensure
      output = reader.value
    end
  end
  File.write(work + '/' + label + '.log', output)
  abort label + ': bounded check timed out' unless status
  abort label + ': ' + output unless status.success? == success
  output
end
rows = []
%w[yes no].each do |missing|
  name = missing == 'yes' ? 'external' : 'in-tree'
  destination = work + '/' + name
  expected = directories + controls + %w[./usr/sbin/ofctl ./usr/sbin/tpctl]
  expected += runtime + %w[./usr/lib/libtsan.so ./usr/lib/libtsan.so.2 ./usr/lib/libtsan.so.2.0] if missing == 'no'
  directories.each { |path| FileUtils.mkdir_p(destination + '/' + path) }
  (expected - directories).each { |path| File.write(destination + '/' + path, "set ownership fixture\n") }
  env = {'MAKE' => wrapper, 'MACHINE' => 'evbarm', 'MACHINE_ARCH' => 'aarch64',
    'AWK' => tools + '/bin/nbawk', 'EGREP' => tools + '/bin/nbgrep -E',
    'HOST_SH' => '/bin/sh', 'DESTDIR' => destination, 'FIXTURE_MISSING' => missing,
    'EXTERNAL_TOOLCHAIN' => nil, 'EMBER_EXTERNAL_GCC_RUNTIME' => nil, 'MAKEFLAGS' => nil}
  run.call(env, name + '-checkflist', ['/bin/sh', sets + '/checkflist', '-L', 'base'])
  selected = run.call(env, name + '-makeflist', ['/bin/sh', sets + '/makeflist', '-L', 'base']).lines.map(&:strip)
  abort name + ': wrong selected inventory' unless selected.sort == expected.sort
  abort name + ': package prefix became OS-owned' if selected.any? { |path| path.start_with?('./usr/pkg') }
  # An omitted OS file must still fail the unmodified strict checker.
  File.unlink(destination + '/bin/sh')
  rejected = run.call(env, name + '-missing-control', ['/bin/sh', sets + '/checkflist', '-L', 'base'], false)
  abort name + ': missing-file rejection lost its cause' unless rejected.lines.map(&:strip).include?('./bin/sh')
  File.write(destination + '/bin/sh', "set ownership fixture\n")
  %w[usr/pkg/gcc16/include/c++/vector usr/pkg/gcc16/lib/libstdc++.so.7 lib/libgcc_s.so.1].each_with_index do |path, index|
    next if missing == 'no' && path == 'lib/libgcc_s.so.1'
    FileUtils.mkdir_p(File.dirname(destination + '/' + path))
    File.write(destination + '/' + path, "forbidden external runtime/SDK fixture\n")
    rejected = run.call(env, name + '-extra-' + index.to_s, ['/bin/sh', sets + '/checkflist', '-L', 'base'], false)
    abort name + ': extra-file rejection lost its cause' unless rejected.lines.map(&:strip).include?('./' + path)
    File.unlink(destination + '/' + path)
    FileUtils.rm_r(destination + '/usr/pkg') if File.directory?(destination + '/usr/pkg')
  end
  run.call(env, name + '-restored', ['/bin/sh', sets + '/checkflist', '-L', 'base'])
  rows << {toolchain_missing: missing, selected: selected.length, package_prefix_owned: false}
end
File.write(work + '/receipt.json', JSON.pretty_generate(rows) + "\n")
puts 'PASS: real external and in-tree set selection/checkflist, with seven missing/extra-file rejection controls'
puts 'SCOPE: host set ownership fixtures; no full distribution, runtime package, ELF closure or device execution'
