#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
# Run the real mkimage/profile with image-tool doubles and a selected mtree.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'rbconfig'
require 'tmpdir'

if ARGV.first == '--tool'
  ARGV.shift
  tool = ARGV.shift
  File.open(ENV.fetch('A733_TEST_LOG'), 'a') { |f| f.puts JSON.generate([tool, ARGV]) }
  case tool
  when 'mtree'
    # A bounded fixture for hosts without NetBSD mtree. This does not prove
    # real parser precedence or filesystem metadata; use --mtree for that.
    if ARGV.include?('-cn')
      tree = ARGV[ARGV.index('-p') + 1]
      entries = ['.'] + Dir.chdir(tree) { Dir.glob('**/*', File::FNM_DOTMATCH).reject { |p| p == '.' || p.split('/').include?('..') } }
      entries.each do |path|
        stat = File.lstat(File.join(tree, path))
        type = stat.directory? ? 'dir' : 'file'
        puts "#{path == '.' ? '.' : './' + path} type=#{type} mode=#{format('%04o', stat.mode & 07777)}"
      end
    else
      entries = {}
      File.foreach(ARGV[ARGV.index('-f') + 1]) do |line|
        next if line.strip.empty? || line.start_with?('#', '/')
        fields = line.split
        entries[fields.first] = fields
      end
      entries.each_value do |fields|
        puts fields.map { |field| { 'uname=root' => 'uid=0', 'gname=wheel' => 'gid=0' }.fetch(field, field) }.join(' ')
      end
    end
  when 'makefs'
    if ARGV[ARGV.index('-t') + 1] == 'ffs'
      at = ARGV.index('-F')
      spec, image, *trees = ARGV[(at + 1)..-1]
      result, status = Open3.capture2e(ENV.fetch('A733_TEST_MTREE'), '-C', '-k', 'type,mode,uid,gid', '-f', spec)
      abort result unless status.success?
      File.write(ENV.fetch('A733_TEST_SPEC'), result)
      abort 'overlay was not the last source' unless trees.last == ENV.fetch('A733_TEST_OVERLAY')
      File.open(image, 'a') { |f| f.truncate(300 * 1024 * 1024) }
    else
      image, boot = ARGV[-2..-1]
      names = %w[netbsd.img boot.scr dtb/allwinner/sun60i-a733-orangepi-zero3w.dtb dtb/allwinner/sun60i-a733-orangepi-zero4.dtb]
      if ENV['A733_TEST_UPDATE']
        names += %w[slotboot.scr boot.scr.uimg]
        {'osgood.dat' => 'A', 'osback.dat' => 'A', 'osnext.dat' => '-'}.each do |file, value|
          abort "wrong initial #{file}" unless File.binread(File.join(boot, file)) == value
        end
        abort 'unexpected prepared slot' if File.exist?(File.join(boot, 'osplan.dat'))
      else
        names << 'dtb/allwinner/sun60i-a733-orangepi-zero3w-diagnostic.dtb'
      end
      names.each do |name|
        abort "missing FAT file #{name}" unless File.file?(File.join(boot, name))
      end
      File.write(image, '')
    end
  when 'mkubootimage'
    abort 'wrong script type' unless ARGV[ARGV.index('-T') + 1] == 'script'
    FileUtils.cp(ARGV[-2], ARGV[-1])
  when 'gpt'
    if ARGV.include?('show')
      image = ARGV[ARGV.index('show') - 1]
      # Detect a finalizer that runs too early: this marker must be replaced.
      File.open(image, 'r+b') { |f| f.seek(8192); f.write('partitioning marker') }
    end
  when 'fdisk'
    # The actual hybrid partition arguments are checked by the caller.
  else
    abort "unexpected tool #{tool}"
  end
  exit
end

options = {}
OptionParser.new do |p|
  p.banner = 'Usage: ruby a733-test.rb (--mtree /absolute/nbmtree | --mock-mtree) [--boot0 FILE --boot-package FILE]'
  p.on('--mtree PATH') { |v| options[:mtree] = File.expand_path(v) }
  p.on('--update-python PATH') { |v| options[:update_python] = File.expand_path(v) }
  p.on('--mock-mtree') { options[:mock_mtree] = true }
  p.on('--boot0 PATH') { |v| options[:boot0] = File.expand_path(v) }
  p.on('--boot-package PATH') { |v| options[:package] = File.expand_path(v) }
end.parse!
abort 'select exactly one of --mtree and --mock-mtree' unless !!options[:mtree] != !!options[:mock_mtree]
abort 'supply both vendor blobs' unless !!options[:boot0] == !!options[:package]
here = File.expand_path(__dir__)
source = File.expand_path('../../..', here)
dtbs = %w[sun60i-a733-orangepi-zero4.dtb sun60i-a733-orangepi-zero3w.dtb
          sun60i-a733-orangepi-zero3w-diagnostic.dtb]

def check(value, message)
  raise message unless value
end

def blob(path, size, package: false)
  data = "\0" * 512
  data[package ? 0 : 4, package ? 13 : 8] = package ? 'sunxi-package' : 'eGON.BT0'
  data[package ? 36 : 16, 4] = [size].pack('V')
  File.binwrite(path, data)
  File.truncate(path, size)
end

Dir.mktmpdir('a733-image-test-') do |work|
  dest = File.join(work, 'dest')
  kernel = File.join(work, 'kernel')
  overlay = File.join(work, 'private overlay')
  bin = File.join(work, 'tools')
  %W[#{dest}/etc/mtree #{dest}/etc/ssh #{dest}/dev #{kernel} #{bin}].each { |d| FileUtils.mkdir_p(d) }
  %w[etc/ssh root/.ssh].each { |d| FileUtils.mkdir_p(File.join(overlay, d)) }
  File.chmod(0700, overlay, File.join(overlay, 'root/.ssh'))
  File.write(File.join(dest, 'etc/mtree/NetBSD.dist'), <<~SPEC)
    . type=dir uname=root gname=wheel mode=0755
    ./etc type=dir uname=root gname=wheel mode=0755
    ./etc/rc.d type=dir uname=root gname=wheel mode=0755
    ./etc/ssh type=dir uname=root gname=wheel mode=0755
    ./root type=dir uname=root gname=wheel mode=0755
    ./dev type=dir uname=root gname=wheel mode=0755
    ./boot type=dir uname=root gname=wheel mode=0755
  SPEC
  File.write(File.join(dest, 'etc/mtree/set.base'), "./etc/ssh/sshd_config type=file uname=root gname=wheel mode=0644\n")
  File.write(File.join(dest, 'etc/mtree/set.comp'), "# compiler set fixture\n")
  File.write(File.join(dest, 'etc/mtree/set.etc'), "./etc/rc.conf type=file uname=root gname=wheel mode=0644\n")
  File.write(File.join(dest, 'etc/rc.conf'), "rc_configured=NO\n")
  File.write(File.join(dest, 'etc/ssh/sshd_config'), "# default fixture\n")
  File.write(File.join(dest, 'dev/MAKEDEV'), "#!/bin/sh\nexit 0\n")
  modes = { 'etc/rc.conf' => 0640, 'etc/ssh/sshd_config' => 0600, 'root/.ssh/authorized_keys' => 0600 }
  modes.each do |name, mode|
    File.write(File.join(overlay, name), 'private fixture: do not print')
    File.chmod(mode, File.join(overlay, name))
  end
  File.binwrite(File.join(kernel, 'netbsd'), "\x7fELF")
  native = "\0" * 64
  native[56, 4] = 'ARMd'
  File.binwrite(File.join(kernel, 'netbsd.img'), native)
  dtbs.each { |name| File.binwrite(File.join(kernel, name), [0xd00dfeed].pack('N')) }
  boot0, package = %w[boot0 package].map { |name| File.join(work, name) }
  blob(boot0, 1024)
  blob(package, 2048, package: true)
  if options[:boot0]
    FileUtils.cp(options[:boot0], boot0)
    FileUtils.cp(options[:package], package)
  end
  selected_tools = %w[makefs mkubootimage gpt fdisk]
  if options[:mock_mtree]
    selected_tools << 'mtree'
    options[:mtree] = File.join(bin, 'mtree')
  end
  selected_tools.each do |tool|
    path = File.join(bin, tool)
    File.write(path, "#!#{RbConfig.ruby}\nexec #{RbConfig.ruby.dump}, #{File.expand_path(__FILE__).dump}, '--tool', #{tool.dump}, *ARGV\n")
    File.chmod(0755, path)
  end
  log, spec = %w[tools.jsonl effective.mtree].map { |name| File.join(work, name) }
  env = {
    'TOOL_MAKEFS' => File.join(bin, 'makefs'), 'TOOL_GPT' => File.join(bin, 'gpt'),
    'TOOL_FDISK' => File.join(bin, 'fdisk'), 'TOOL_MKUBOOTIMAGE' => File.join(bin, 'mkubootimage'),
    'TOOL_MTREE' => options[:mtree], 'HOST_SH' => '/bin/sh',
    'A733_BOOT0' => boot0, 'A733_BOOT_PACKAGE' => package,
    'A733_TEST_LOG' => log, 'A733_TEST_SPEC' => spec,
    'A733_TEST_OVERLAY' => overlay, 'A733_TEST_MTREE' => options[:mtree]
  }
  image = File.join(work, 'a733.img')
  command = ['/bin/sh', File.join(here, 'mkimage'), '-h', 'a733', '-S', source,
             '-D', dest, '-K', kernel, '-c', overlay, image]
  output, status = Open3.capture2e(env, *command)
  check(status.success?, "mkimage failed:\n#{output}")
  check(!output.include?('private fixture'), 'overlay contents leaked')
  check(File.stat(image).mode & 0777 == 0600, 'new image is not private')
  File.chmod(0644, image)
  output, status = Open3.capture2e(env, *command)
  check(status.success?, "image rebuild failed:\n#{output}")
  check(File.stat(image).mode & 0777 == 0600, 'rebuilt image is not private')
  metadata = File.readlines(spec).map { |line| line.split }.to_h { |fields| [fields.first, fields.drop(1)] }
  { '.' => 0755, './root/.ssh' => 0700 }.merge(modes.transform_keys { |k| './' + k }).each do |path, mode|
    fields = metadata.fetch(path)
    value = fields.find { |s| s.start_with?('mode=') }.split('=').last.to_i(8)
    check(value == mode, "wrong image mode for #{path}")
    check(fields.include?('uid=0') && fields.include?('gid=0'), "wrong image owner for #{path}")
  end
  [[boot0, 8192], [package, 16793600]].each do |file, offset|
    File.open(image, 'rb') { |f| f.seek(offset); check(f.read(File.size(file)) == File.binread(file), 'vendor bytes differ') }
  end
  calls = File.readlines(log).map { |line| JSON.parse(line) }
  check(calls.any? { |tool, args| tool == 'gpt' && args.include?('create') && args[-2..-1] == ['-p', '16'] }, 'wrong GPT table size')
  check(calls.any? { |tool, args| tool == 'fdisk' && args.include?('12/65536/524288') && args.include?('-0') }, 'wrong hybrid FAT partition')
  check(calls.any? { |tool, args| tool == 'gpt' && args.include?('netbsd-root') && args.include?('589824') }, 'wrong root partition')

  if options[:update_python]
    revision = '1' * 40
    tag = "EmberBSD:#{revision}\n\0NetBSD 11.1 (EMBER64)".b
    File.open(File.join(kernel, 'netbsd'), 'ab') { |file| file.write(tag) }
    File.open(File.join(kernel, 'netbsd.img'), 'ab') { |file| file.write(tag) }
    FileUtils.mkdir_p(File.join(kernel, 'modules'))
    %w[bcm2712btcom bcmexpgpio rp1wmcodec rpi5button].each do |name|
      File.write(File.join(kernel, 'modules', name + '.kmod'), name)
    end
    %w[usr/local/libexec usr/local/etc usr/pkg/bin].each { |name| FileUtils.mkdir_p(File.join(overlay, name)) }
    File.write(File.join(overlay, 'usr/local/libexec/ember-sdk.pyz'), "print('fetch-os-update')")
    File.binwrite(File.join(overlay, 'usr/pkg/bin/python3.15'), "\x7fELF\x02\x01".b + "\0" * 12 + "\xb7\x00".b)
    target = {arch: 'aarch64', triplet: 'aarch64--netbsd', abi: 'netbsd-11', runtime: 'libc++'}
    File.write(File.join(overlay, 'usr/local/etc/ember-sdk.json'),
               JSON.generate({repositories: {native: {install_mode: 'native', target: target}}}))
    archive = File.join(work, 'etc.tar.xz')
    File.write(archive, 'verified etc fixture')
    update_env = env.merge('A733_UPDATE_PYTHON' => options[:update_python], 'A733_UPDATE_OS_COMMIT' => revision,
                           'A733_UPDATE_ETC' => archive, 'A733_UPDATE_ETC_SHA256' => Digest::SHA256.file(archive).hexdigest,
                           'A733_TEST_UPDATE' => '1')
    update_command = command.map { |arg| arg == 'a733' ? 'a733-update' : arg }
    result, state = Open3.capture2e(update_env, *update_command)
    check(state.success?, "two-slot mkimage failed:\n#{result}")
    additions = File.readlines(log).map { |line| JSON.parse(line) }.select { |tool, args| tool == 'gpt' && args.include?('add') }
    a, b = additions.last(2).map(&:last)
    root_sector = ->(args, flag) { Integer(args.fetch(args.index(flag) + 1)) }
    check(a.include?('netbsd-root') && b.include?('ember-root-b'), 'wrong root labels')
    check(root_sector.call(a, '-s') == root_sector.call(b, '-s') &&
          root_sector.call(a, '-b') + root_sector.call(a, '-s') == root_sector.call(b, '-b'), 'slots overlap or differ in size')
    meta = File.read(spec)
    %w[usr/libexec/ember-os-update usr/libexec/ember-os-slot etc/ember-os-health etc/rc.d/ember_os_update
       etc/rc.conf.d/ember_os_update etc/rc.conf.d/resize_root etc/rc.conf.d/resize_gpt
       var/db/ember-os-update/release.json var/db/ember-os-update/etc.tar.xz].each do |path|
      check(meta.include?("./#{path} "), "two-slot root omits #{path}")
    end
    result, state = Open3.capture2e(update_env.merge('A733_UPDATE_ETC_SHA256' => '0' * 64), *update_command)
    check(!state.success? && result.include?('SHA-256 mismatch'), 'wrong baseline etc accepted')
    File.write(image, 'keep the previous image')
    File.chmod(0640, image)
    result, state = Open3.capture2e(update_env, *update_command[0...-1], '-s', '16', image)
    check(!state.success? && result.include?('too small'), 'short image accepted')
    check(File.binread(image) == 'keep the previous image' && File.stat(image).mode & 0777 == 0640,
          'rejected slot size changed the previous image')
    check(Dir.glob(image + '.??????').empty?, 'rejected image left a staging file')
  end

  # Every rejected input must leave an existing output and the tool log intact.
  reject = lambda do |message|
    File.write(image, 'unchanged')
    File.chmod(0644, image)
    previous = File.binread(log)
    result, state = Open3.capture2e(env, *command)
    check(!state.success? && result.include?(message), "wrong rejection: #{result}")
    check(File.binread(image) == 'unchanged' && File.binread(log) == previous, 'rejection wrote the image')
    check(File.stat(image).mode & 0777 == 0644, 'rejection changed the image mode')
  end
  original = File.binread(boot0)
  File.binwrite(boot0, 'bad signature')
  reject.call('signature')
  File.binwrite(boot0, original)
  File.open(boot0, 'r+b') { |f| f.seek(16); f.write([513].pack('V')) }
  reject.call('length')
  File.binwrite(boot0, original)
  File.open(boot0, 'ab') { |f| f.write("\0" * 512) }
  reject.call('length')
  blob(boot0, 17 * 1024 * 1024)
  reject.call('overlaps')
  File.binwrite(boot0, original)
  original_package = File.binread(package)
  blob(package, 17 * 1024 * 1024, package: true)
  reject.call('overlaps')
  File.binwrite(package, original_package)
  File.open(package, 'r+b') { |f| f.seek(36); f.write([513].pack('V')) }
  reject.call('length')
  File.binwrite(package, original_package)
  File.write(File.join(overlay, 'netbsd'), 'mismatched kernel')
  reject.call('selected kernel')
  File.unlink(File.join(overlay, 'netbsd'))
  missing = File.join(kernel, dtbs.last)
  File.rename(missing, missing + '.saved')
  reject.call('kernel-directory')
  File.rename(missing + '.saved', missing)
  target = File.join(work, 'symlink-target')
  File.write(target, 'unrelated target')
  File.chmod(0644, target)
  File.unlink(image)
  File.symlink(target, image)
  previous = File.binread(log)
  result, state = Open3.capture2e(env, *command)
  check(!state.success? && result.include?('symbolic link'), 'output symlink was not rejected')
  check(File.binread(target) == 'unrelated target' && File.stat(target).mode & 0777 == 0644, 'symlink target changed')
  check(File.binread(log) == previous, 'symlink rejection invoked image tools')
  File.unlink(image)
  File.unlink(File.join(dest, 'etc/mtree/set.comp'))
  reject.call('manifest')
end
puts 'PASS: A733 source contract, vendor bytes and pre-write rejection'
puts(options[:mock_mtree] ? 'mtree was a test double; real metadata precedence remains untested.' : 'Real mtree metadata precedence passed.')
puts 'Image tools were test doubles; filesystem generation and physical boot remain untested.'
