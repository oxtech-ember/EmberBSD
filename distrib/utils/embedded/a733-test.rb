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
      if ENV['SLOT_TEST_PROFILE']
        policy = trees.pop
        abort 'unexpected policy overlay' unless Dir.chdir(policy) { Dir.glob('**/*').sort } == %w[etc etc/rc.conf]
        site = File.binread(File.join(ENV.fetch('A733_TEST_OVERLAY'), 'etc/rc.conf'))
        abort 'site rc.conf was lost' unless File.binread(File.join(policy, 'etc/rc.conf')).start_with?(site)
        abort 'native updater was not enabled' unless File.read(File.join(policy, 'etc/rc.conf')).lines.include?("ember_update=YES\n")
      end
      abort 'site overlay lost precedence' unless trees.last == ENV.fetch('A733_TEST_OVERLAY')
      File.open(image, 'a') { |f| f.truncate(300 * 1024 * 1024) }
    else
      image, boot = ARGV[-2..-1]
      names = %w[netbsd.img boot.scr dtb/allwinner/sun60i-a733-orangepi-zero3w.dtb dtb/allwinner/sun60i-a733-orangepi-zero4.dtb]
      if (description = ENV['SLOT_TEST_PROFILE'])
        profile = JSON.parse(File.read(description))
        slots = profile.fetch('slots')
        names = ['netbsd.img', 'boot.scr'] + slots.fetch('trees').map { |dtb| File.join(slots.fetch('tree_directory'), dtb) }
        root = File.dirname(boot)
        abort 'wrong installed adapter profile' unless JSON.parse(File.read(File.join(root, 'etc/ember-update-profile.json'))) == slots
        receipt = File.binread(File.join(root, 'var/db/ember-update/os-release.json'))
        if ENV['OS_UPDATE_RELEASE']
          abort 'installed OS descriptor bytes changed' unless receipt == File.binread(ENV.fetch('OS_UPDATE_RELEASE'))
        else
          abort 'initial build has a published generation' unless JSON.parse(receipt).fetch('generation') == 0
        end
        fstab = File.read(File.join(root, 'etc/fstab'))
        abort 'wrong root label' unless fstab.match?(/^NAME=#{Regexp.escape(slots.fetch('roots').fetch('A'))}\s+\/\s/)
        abort 'wrong boot label' unless fstab.match?(/^NAME=#{Regexp.escape(slots.fetch('esp'))}\s+\/boot\s/)
        selector = File.read(File.join(boot, 'boot.scr.uimg'))
        slots.fetch('roots').each_value { |label| abort 'wrong selector labels' unless selector.include?(label) }
        %w[marker_address script_address].each { |key| abort 'wrong RAM address' unless selector.include?(profile.fetch(key)) }
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
    File.open(File.join(kernel, 'netbsd'), 'r+b') do |f|
      f.seek(4)
      f.write("\x02\x01".b)
      f.seek(18)
      f.write("\xb7\0".b)
    end
    tag = "EmberBSD:#{revision}\n\0NetBSD 11.1 (EMBER64)".b
    File.open(File.join(kernel, 'netbsd'), 'ab') { |file| file.write(tag) }
    File.open(File.join(kernel, 'netbsd.img'), 'ab') { |file| file.write(tag) }
    FileUtils.mkdir_p(File.join(kernel, 'modules'))
    %w[bcm2712btcom bcmexpgpio rp1wmcodec rpi5button].each do |name|
      File.write(File.join(kernel, 'modules', name + '.kmod'), name)
    end
    %w[usr/sbin/ember-update usr/libexec/ember-os-slot usr/libexec/ember-install-slot
       usr/libexec/ember-stage-ffs etc/rc.d/ember_update].each do |name|
      path = File.join(dest, name)
      FileUtils.mkdir_p(File.dirname(path))
      FileUtils.cp(File.join(kernel, 'netbsd'), path)
      File.chmod(0555, path)
    end
    FileUtils.mkdir_p(File.join(dest, 'etc/openssl'))
    FileUtils.cp(File.join(source, 'etc/emberbsd-release-root.pem'), File.join(dest, 'etc/openssl'))
    archive = File.join(work, 'etc.tar.xz')
    File.write(archive, 'verified etc fixture')
    profile = File.join(source, 'ember/image/os-profiles/orangepi-zero3w.json')
    update_env = env.merge('OS_UPDATE_PYTHON' => options[:update_python], 'OS_UPDATE_OS_COMMIT' => revision,
                           'OS_UPDATE_ETC' => archive, 'OS_UPDATE_ETC_SHA256' => Digest::SHA256.file(archive).hexdigest,
                           'SLOT_TEST_PROFILE' => profile)
    update_command = command.map { |arg| arg == 'a733' ? 'orangepi-zero3w' : arg }.insert(-2, '-U', profile)
    result, state = Open3.capture2e(update_env, *update_command)
    check(state.success?, "two-slot mkimage failed:\n#{result}")
    File.open(image, 'rb') do |f|
      f.seek(131072)
      check(f.read(File.size(boot0)) == File.binread(boot0), 'wrong Zero 3W boot0 location')
    end
    additions = File.readlines(log).map { |line| JSON.parse(line) }.select { |tool, args| tool == 'gpt' && args.include?('add') }
    a, b = additions.last(2).map(&:last)
    root_sector = ->(args, flag) { Integer(args.fetch(args.index(flag) + 1)) }
    check(a.include?('netbsd-root') && b.include?('ember-root-b'), 'wrong root labels')
    check(root_sector.call(a, '-s') == root_sector.call(b, '-s') &&
          root_sector.call(a, '-b') + root_sector.call(a, '-s') == root_sector.call(b, '-b'), 'slots overlap or differ in size')
    meta = File.read(spec)
    %w[etc/ember-update-profile.json etc/ember-os-health etc/rc.conf.d/resize_root etc/rc.conf.d/resize_gpt
       var/db/ember-update/os-release.json var/db/ember-update/etc.tar.xz].each do |path|
      check(meta.include?("./#{path} "), "two-slot root omits #{path}")
    end
    # An independent board supplies its own boot script, RAM and hardware data.
    fixture = File.join(work, 'second board')
    FileUtils.mkdir_p(File.join(fixture, 'conf'))
    FileUtils.cp(File.join(here, 'mkimage'), fixture)
    FileUtils.ln_s(File.join(here, 'files'), File.join(fixture, 'files'))
    FileUtils.cp(File.join(here, 'conf/evbarm.conf'), File.join(fixture, 'conf'))
    File.write(File.join(fixture, 'conf/fixture.conf'), <<~'CONF')
      board=fixture
      gpt=true
      root_overlay=true
      os_update_adapter=uboot-ffs
      sets="base comp etc"
      MKDTB=no
      . "${DIR}/conf/evbarm.conf"
      make_fstab() { make_fstab_evbarm; }
      customize() { customize_evbarm; }
      populate() {
          cp "$kernel/netbsd" "$mnt/netbsd"
          cp "$kernel/netbsd.img" "$mnt/boot/netbsd.img"
          mkdir -p "$mnt/boot/dtb/fixture"
          for tree in $boot_trees; do cp "$kernel/$tree" "$mnt/boot/dtb/fixture/"; done
          cp "$FIXTURE_BOOT" "$mnt/boot/boot.scr"
          echo './netbsd type=file uname=root gname=wheel mode=0755' >> "$tmp/selected_sets"
      }
    CONF
    alternate = JSON.parse(File.read(profile))
    boot_script = File.join(fixture, 'launch.cmd')
    File.write(boot_script, "echo fixture board boot\n")
    alternate.merge!('image' => 'fixture', 'boot_script' => boot_script,
                     'marker_address' => '0x100000', 'script_address' => '0x110000')
    alternate.fetch('slots').merge!('profile' => 'fixture-stock', 'model' => 'fixture,board',
                                    'roots' => {'A' => 'fixture-a', 'B' => 'fixture-b'}, 'esp' => 'fixture-esp',
                                    'watchdog' => 'fixturewdt0', 'tree_directory' => 'dtb/fixture',
                                    'trees' => ['fixture.dtb'], 'modules' => [])
    FileUtils.cp(File.join(kernel, dtbs.first), File.join(kernel, 'fixture.dtb'))
    second = File.join(fixture, 'fixture.json')
    File.write(second, JSON.generate(alternate))
    alt_command = ['/bin/sh', File.join(fixture, 'mkimage'), '-h', 'fixture', '-S', source,
                   '-D', dest, '-K', kernel, '-c', overlay, '-U', second, File.join(fixture, 'test.img')]
    result, state = Open3.capture2e(update_env.merge('SLOT_TEST_PROFILE' => second, 'FIXTURE_BOOT' => boot_script), *alt_command)
    check(state.success?, "independent board failed: #{result}")
    slots = alternate.fetch('slots')
    descriptor = {'schema' => 'emberbsd.os-update.v1', 'generation' => 7, 'os_commit' => revision,
                  'profile' => slots.fetch('profile'), 'target' => slots.fetch('target'),
                  'sets' => slots.fetch('sets').to_h { |role| [role, "test-#{role}"] }}
    head = File.join(work, 'update.json')
    File.write(head, JSON.generate(descriptor) + "\n\n")
    enrolled = update_env.merge('SLOT_TEST_PROFILE' => second, 'FIXTURE_BOOT' => boot_script, 'OS_UPDATE_RELEASE' => head,
                                'OS_UPDATE_RELEASE_SHA256' => Digest::SHA256.file(head).hexdigest)
    result, state = Open3.capture2e(enrolled, *alt_command)
    check(state.success?, "published baseline failed: #{result}")
    File.write(alt_command.last, 'existing image')
    result, state = Open3.capture2e(enrolled.merge('OS_UPDATE_RELEASE_SHA256' => '0' * 64), *alt_command)
    check(!state.success? && result.include?('SHA-256 mismatch'), 'unverified OS generation accepted')
    File.write(head, JSON.generate(descriptor.merge('os_commit' => '2' * 40)))
    result, state = Open3.capture2e(enrolled.merge('OS_UPDATE_RELEASE_SHA256' => Digest::SHA256.file(head).hexdigest), *alt_command)
    check(!state.success? && result.include?('differs from the image profile or kernel'), 'wrong generation installed')
    File.write(head, JSON.generate(descriptor).sub('"generation":7', '"generation":1,"generation":7'))
    result, state = Open3.capture2e(enrolled.merge('OS_UPDATE_RELEASE_SHA256' => Digest::SHA256.file(head).hexdigest), *alt_command)
    check(!state.success? && result.include?('duplicate OS descriptor field'), 'duplicate generation accepted')
    check(File.read(alt_command.last) == 'existing image', 'wrong generation changed the published image')
    File.write(image, 'previous image')
    wrong = update_command.map { |arg| arg == profile ? second : arg }
    result, state = Open3.capture2e(update_env, *wrong)
    check(!state.success? && result.include?('does not match the image board') &&
          File.binread(image) == 'previous image', 'foreign profile replaced the image')
    module_dir = File.join(overlay, 'stand/evbarm/11.1/modules/rpi5button')
    FileUtils.mkdir_p(module_dir)
    conflicting_module = File.join(module_dir, 'rpi5button.kmod')
    File.write(conflicting_module, 'different module')
    result, state = Open3.capture2e(update_env, *update_command)
    check(!state.success? && result.include?('overlay overrides profile-owned path') &&
          File.binread(image) == 'previous image', 'untrusted overlay module replaced the selected module')
    File.unlink(conflicting_module)
    fstab = File.join(overlay, 'etc/fstab')
    File.write(fstab, "NAME=wrong / ffs rw 1 1\n")
    result, state = Open3.capture2e(update_env, *update_command)
    check(!state.success? && result.include?('overrides profile-owned path: etc/fstab'), 'custom root label accepted')
    File.unlink(fstab)
    elf = File.join(kernel, 'netbsd')
    File.open(elf, 'r+b') { |file| file.seek(18); file.write([62].pack('v')) }
    result, state = Open3.capture2e(update_env, *update_command)
    check(!state.success? && result.include?('kernel ELF differs'), 'wrong kernel architecture accepted')
    File.open(elf, 'r+b') { |file| file.seek(18); file.write([183].pack('v')) }
    result, state = Open3.capture2e(update_env.merge('OS_UPDATE_ETC_SHA256' => '0' * 64), *update_command)
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
