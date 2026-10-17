# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Check real set selection against header owners.
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'timeout'

abort 'Usage: unwind-setlist.rb SOURCE_ROOT NATIVE_TOOLDIR NEW_OUTPUT' unless ARGV.length == 3
root, tools = ARGV.first(2).map { |p| File.realpath(p) }
work = File.expand_path(ARGV[2])
abort 'new absolute output required' unless ARGV[2].start_with?('/') && !File.exist?(work)
Dir.mkdir(work)
sets = work + '/sets'
FileUtils.mkdir_p(sets + '/lists/base')
FileUtils.mkdir_p(sets + '/lists/comp')
Dir.children(root + '/distrib/sets/lists').each do |name|
  next unless File.directory?(root + '/distrib/sets/lists/' + name)
  FileUtils.mkdir_p(sets + '/lists/' + name)
  File.write(sets + '/lists/' + name + '/mi', '')
end
%w[checkflist makeflist sets.subr mkvars.mk].each do |name|
  FileUtils.cp(root + '/distrib/sets/' + name, sets + '/' + name)
end
# Keep the actual affected records and a normal libc header control. The
# fixture exercises the unmodified set parser and checker, not their full tree.
base = File.readlines(root + '/distrib/sets/lists/base/mi')
File.write(sets + '/lists/base/mi', base.select { |l| %w[. ./usr ./usr/include].include?(l.split.first) }.join)
comp = File.readlines(root + '/distrib/sets/lists/comp/mi')
File.write(sets + '/lists/comp/mi', comp.select { |l| %w[./usr/include/unwind.h ./usr/include/stdlib.h].include?(l.split.first) }.join)
wrapper = work + '/make'
arguments = [tools + '/bin/nbmake', '-m', root + '/share/mk', 'NETBSDSRCDIR=' + root,
  'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'TOOLCHAIN_MISSING=no', 'USETOOLS=no',
  'MKCOMPAT=no', 'MKX11=no', 'MKKMOD=no', 'MKLIBCXX=no', 'MKCXX=yes', 'MAKEOBJDIR=' + work]
File.write(wrapper, "#!/bin/sh\nexec " + Shellwords.join(arguments) +
  " MKGCC=\"$FIXTURE_GCC\" MKGCCCMDS=no HAVE_LIBGCC_EH=\"$FIXTURE_EH\" \"$@\"\n")
File.chmod(0755, wrapper)
run = lambda do |env, label, command, expected = true|
  text = ''
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
      text = reader.value
    end
  end
  File.write(work + '/' + label + '.log', text)
  abort label + ': bounded metadata check timed out' unless status
  abort label + ': ' + text unless status.success? == expected
  text
end
rows = []
[['no', 'yes', false], ['yes', 'yes', true], ['no', 'no', true], ['yes', 'no', true]].each do |gcc, eh, wanted|
  name = 'gcc-' + gcc + '-eh-' + eh
  destination = work + '/' + name
  FileUtils.mkdir_p(destination + '/usr/include')
  File.write(destination + '/usr/include/stdlib.h', "source-contract header fixture\n")
  File.write(destination + '/usr/include/unwind.h', "source-contract header fixture\n") if wanted
  env = {'MAKE' => wrapper, 'MACHINE' => 'amd64', 'MACHINE_ARCH' => 'x86_64',
    'AWK' => tools + '/bin/nbawk', 'EGREP' => tools + '/bin/nbgrep -E',
    'HOST_SH' => '/bin/sh', 'DESTDIR' => destination,
    'FIXTURE_GCC' => gcc, 'FIXTURE_EH' => eh,
    'EXTERNAL_TOOLCHAIN' => nil, 'EMBER_EXTERNAL_GCC_RUNTIME' => nil, 'MAKEFLAGS' => nil}
  values = {}
  {'graph' => ['lib', '${SUBDIR}'],
    'libgcc' => ['external/gpl3/gcc/lib/libgcc/libgcc', '${INCS}'],
    'libc' => ['lib/libc', '${INCS}']}.each do |key, (directory, variable)|
    values[key] = run.call(env, name + '-owner-' + key,
      [wrapper, '-C', root + '/' + directory, '-V', variable]).split
  end
  gcc_selected = values['graph'].any? { |path| path.end_with?('/lib/libgcc') }
  gcc_header = gcc_selected && values['libgcc'].include?('unwind.h')
  libc_header = values['libc'].include?('unwind.h')
  abort name + ': actual INCS owner differs from the expected predicate' unless (gcc_header || libc_header) == wanted
  run.call(env, name + '-checkflist', ['/bin/sh', sets + '/checkflist', '-L', 'base'])
  flist = run.call(env, name + '-makeflist', ['/bin/sh', sets + '/makeflist', 'comp']).lines.map(&:strip)
  abort name + ': duplicate or incorrect header selection' unless flist.count('./usr/include/unwind.h') == (wanted ? 1 : 0)
  abort name + ': ordinary libc header disappeared' unless flist.include?('./usr/include/stdlib.h')
  # Missing installed headers and forbidden extra files must still fail the
  # actual checker. No allow-missing or allow-extra switches are used.
  if wanted
    File.unlink(destination + '/usr/include/unwind.h')
  else
    File.write(destination + '/usr/include/unwind.h', "unexpected header fixture\n")
  end
  rejected = run.call(env, name + '-reject-unwind-mismatch', ['/bin/sh', sets + '/checkflist', '-L', 'base'], false)
  abort name + ': rejection did not identify the unwind header' unless rejected.lines.map(&:strip).include?('./usr/include/unwind.h')
  File.unlink(destination + '/usr/include/unwind.h') if File.exist?(destination + '/usr/include/unwind.h')
  File.write(destination + '/usr/include/unwind.h', "source-contract header fixture\n") if wanted
  File.unlink(destination + '/usr/include/stdlib.h')
  rejected = run.call(env, name + '-reject-ordinary-header', ['/bin/sh', sets + '/checkflist', '-L', 'base'], false)
  abort name + ': rejection did not identify the ordinary header' unless rejected.lines.map(&:strip).include?('./usr/include/stdlib.h')
  rows << {mkgcc: gcc, have_libgcc_eh: eh, gcc_header: gcc_header, libc_header: libc_header, selected: wanted}
end
File.write(work + '/receipt.json', JSON.pretty_generate(rows) + "\n")
puts 'PASS: four real INCS/setlist/checkflist cases, plus eight missing/extra-file rejection controls'
puts 'SCOPE: host metadata fixtures only; no header installation, compilation, complete distribution or target execution'
