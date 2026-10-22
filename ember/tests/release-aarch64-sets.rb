# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted), 2026-10-16.
# Exercise actual makeflist selectors. Optional CI metadata checks names only;
# empty placeholders cannot establish archive bytes or target execution.
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
source = File.expand_path(ENV.fetch('EMBER_TEST_SOURCE', root))
make = ARGV.shift || ENV.fetch('BMAKE', 'bmake')
abort 'Use release-aarch64-sets.rb [BMAKE]' unless ARGV.empty?
sets = File.join(root, 'distrib/sets')
def check(condition, message)
  abort 'FAIL: ' + message unless condition
end

Dir.mktmpdir('ember-aarch64-sets') do |dir|
  wrapper = File.join(dir, 'make')
  File.write(wrapper, "#!/bin/sh\nexec #{Shellwords.escape(make)} -m #{Shellwords.escape(File.join(source, 'share/mk'))} \"$@\"\n")
  File.chmod(0755, wrapper)
  base_environment = {'PATH' => ENV.fetch('PATH'), 'HOME' => ENV.fetch('HOME', '/'), 'LC_ALL' => 'C',
    'MAKE' => wrapper, 'MAKECONF' => '/dev/null', 'NETBSDSRCDIR' => source,
    'MACHINE' => 'evbarm', 'MACHINE_ARCH' => 'aarch64', 'MACHINE_CPU' => 'aarch64'}
  flags = %w[HAVE_GCC=12 TOOLCHAIN_MISSING=no MKGCC=yes MKGCCCMDS=no MKCXX=yes MKLIBSTDCXX=yes
    MKLINKLIB=yes MKSTATICLIB=yes MKPIC=yes MKPICLIB=yes MKDYNAMICROOT=yes MKFIRMWARE=yes
    MKX11=no MKRUMP=no MKCOMPAT=no MKDEBUG=no MKDEBUGLIB=no MKDEBUGKERNEL=no MKKMOD=no
    MKATF=no MKKYUA=no MKMAN=no MKHTML=no MKDOC=no MKINFO=no MKLINT=no MKPROFILE=no
    MKGROFF=no MKCVS=no MKGDB=no MKBINUTILS=no].to_h { |value| value.split('=', 2) }
  list = lambda do |options, *names|
    output, error, result = Open3.capture3(base_environment.merge(options), 'sh', File.join(sets, 'makeflist'),
      *names, chdir: source, unsetenv_others: true)
    check(result.success? && error.empty?, "real makeflist failed: #{output}#{error}")
    output.lines.map(&:strip)
  end
  native = list.call(flags, 'base', 'comp')
  check(native.none? { |path| path == './usr/pkg' || path.start_with?('./usr/pkg/gcc16') }, 'in-tree build requires a staged GCC16 package')
  %w[./lib/libgcc.a ./lib/libgcc_eh.a ./lib/libstdc++.a ./lib/libstdc++.so ./lib/libstdc++.so.7 ./lib/libstdc++.so.7.36].each do |path|
    check(!native.include?(path), 'in-tree build requires external runtime alias ' + path)
  end
  %w[./lib/libgcc_s.so.1.0 ./usr/lib/libgcc.a ./usr/lib/libstdc++.a ./usr/lib/libstdc++.so.9.1
     ./usr/include/g++/vector ./usr/include/stdio.h ./usr/lib/crt0.o].each do |path|
    check(native.include?(path), 'in-tree runtime/development file was removed: ' + path)
  end
  check(!native.include?('./usr/sbin/mdsetimage'), 'profile omits both mdsetimage producers but still requires the command')
  manuals = {'MKMAN' => 'yes', 'MKHTML' => 'yes', 'MKCATPAGES' => 'yes'}
  binutils = list.call(flags.merge(manuals).merge('MKBINUTILS' => 'yes'), 'base', 'man', 'manhtml')
  external = list.call(flags.merge(manuals).merge('TOOLCHAIN_MISSING' => 'yes'), 'base', 'comp', 'man', 'manhtml')
  %w[./usr/sbin/mdsetimage ./usr/share/man/cat8/mdsetimage.0 ./usr/share/man/man8/mdsetimage.8
     ./usr/share/man/html8/mdsetimage.html].each do |path|
    check(binutils.include?(path), 'binutils producer lost ' + path)
    check(external.include?(path), 'external fallback producer lost ' + path)
  end
  check(external.none? { |path| path == './usr/pkg' || path.start_with?('./usr/pkg/gcc16') },
    'external compiler makes OS sets own the separate Ports runtime/SDK payload')
  %w[./lib/libgcc.a ./lib/libstdc++.so.7.36 ./usr/include/g++/vector].each do |path|
    check(!external.include?(path), 'external build requires a duplicate compiler runtime input ' + path)
  end
  %w[./usr/include/stdio.h ./usr/lib/crt0.o].each do |path|
    check(external.include?(path), 'external compiler removed an OS-owned development input ' + path)
  end
  check(!external.include?('./usr/lib/libstdc++.so.9.1'), 'external profile still requires the in-tree GCC12 C++ runtime')
  puts 'PASS: real AArch64 set selection preserves OS/Ports runtime ownership and both mdsetimage producers'

  if ENV['EMBER_TEST_METALOG']
    metadata = File.expand_path(ENV.fetch('EMBER_TEST_METALOG'))
    receipt = JSON.parse(File.read(ENV.fetch('EMBER_TEST_BUILD_PROFILE')))
    actual_flags = receipt.fetch('commands').last.each_cons(2).select { |key, _| key == '-V' }
      .map(&:last).to_h { |value| value.split('=', 2) }
    destination = File.join(dir, 'metadata-only-dest')
    Dir.mkdir(destination)
    # Sanitise duplicate records as the normal unprivileged build does. Only
    # names and directory structure are recreated. Link/file data are absent.
    names = File.readlines(metadata).reject { |line| line.start_with?('#') || line.strip.empty? }
      .map { |line| line.split(/\s+/) }.to_h do |fields|
        # install(1) can record a repeated separator. The filesystem and the
        # build's mtree sanitiser treat that as the same pathname.
        [fields.first.gsub(%r{/+}, '/'), fields.drop(1)]
      end
    names.sort.each do |name, attributes|
      next if name == '.' || name == './'
      check(name.start_with?('./') && name.delete_prefix('./').split('/').none? { |part| part.empty? || %w[. ..].include?(part) }, 'unsafe retained metadata path: ' + name)
      path = File.join(destination, name.delete_prefix('./'))
      if attributes.include?('type=dir')
        FileUtils.mkdir_p(path)
      else
        FileUtils.mkdir_p(File.dirname(path))
        File.write(path, '')
      end
    end
    invoke = lambda do
      Open3.capture3(base_environment.merge(actual_flags).merge('DESTDIR' => destination),
        'sh', File.join(sets, 'checkflist'), '-L', 'base', '-M', metadata,
        chdir: source, unsetenv_others: true)
    end
    output, error, result = invoke.call
    check(result.success?, "retained full METALOG names disagree with strict checkflist: #{output}#{error}")
    File.unlink(File.join(destination, 'usr/include/stdio.h'))
    output, error, result = invoke.call
    check(!result.success? && output.include?('./usr/include/stdio.h'), "strict missing-file control was accepted: #{output}#{error}")
    File.write(File.join(destination, 'usr/include/stdio.h'), '')
    File.write(File.join(destination, 'foreign-runtime'), '')
    output, error, result = invoke.call
    check(!result.success? && output.include?('./foreign-runtime'), "strict extra-file control was accepted: #{output}#{error}")
    puts "PASS: strict checkflist accepts all #{names.length} retained CI METALOG names and rejects missing/extra controls (metadata only)"
  end
end
