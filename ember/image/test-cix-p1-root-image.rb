#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Execute the actual CIX root stager with synthetic inputs and boundary tools.
# Run: ruby ember/image/test-cix-p1-root-image.rb
# Review an unpublished candidate without changing it:
# ROOT_SCRIPT=/absolute/path/to/cix-p1-root.sh ruby ember/image/test-cix-p1-root-image.rb
# No firmware download, real filesystem image, block device or private profile is used.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'
require 'shellwords'
require 'tmpdir'

ROOT_SCRIPT = File.expand_path(ENV.fetch('ROOT_SCRIPT', File.join(__dir__, 'cix-p1-root.sh')))
abort 'Root stager is missing' unless File.file?(ROOT_SCRIPT)
PROFILE_FILES = %w[cix-p1-root.sh cix-p1-root.subr cix-p1-network.subr cix-p1-power.subr cix-p1-power-button cix-p1-rc cix-p1-rc.shutdown cix-p1-console].freeze
# Capture one immutable candidate, without editing another agent's checkout.
PROFILE_DATA = PROFILE_FILES.to_h do |file|
  path = file == 'cix-p1-root.sh' ? ROOT_SCRIPT : File.join(File.dirname(ROOT_SCRIPT), file)
  [file, File.binread(path)]
end.freeze
REQUIRED = %w[sbin/init sbin/ifconfig sbin/fsck sbin/fsck_ffs sbin/mount sbin/mount_tmpfs sbin/shutdown sbin/rndctl usr/sbin/wpa_supplicant usr/sbin/wpa_cli sbin/dhcpcd bin/sh bin/stty usr/sbin/usbdevs usr/bin/stat usr/libexec/getty usr/sbin/powerd].freeze
RUNTIME = (REQUIRED + %w[usr/bin/wall usr/bin/netstat]).freeze
DIAGNOSTIC_TARGETS = (%w[usb] + (0..19).map { |i| "usb#{i}" } + %w[plcom1 plcom3]).freeze
FW_HASH = '36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26'
HOST_PATH = [File.dirname(RbConfig.ruby), '/usr/bin', '/bin', '/usr/sbin', '/sbin'].uniq.join(':')
REAL_SHASUM = HOST_PATH.split(':').map { |p| File.join(p, 'shasum') }.find { |p| File.executable?(p) }
abort 'Host shasum is missing' unless REAL_SHASUM
# Use the unchanged native generator and AArch64 major-number sources.
# MAKEDEV -s emits metadata; it cannot create a host device node.
native_source = File.expand_path('../..', File.dirname(ROOT_SCRIPT))
NATIVE_MAKEDEV, generator_error, generator_status = Open3.capture3(
  {'MACHINE' => 'aarch64', 'MACHINE_ARCH' => 'aarch64', 'NETBSDSRCDIR' => native_source},
  'awk', '-f', native_source + '/etc/MAKEDEV.awk', native_source + '/etc/MAKEDEV.tmpl')
abort generator_error unless generator_status.success?
diagnostic_output, diagnostic_error, diagnostic_status = Open3.capture3(
  {'MACHINE' => 'aarch64', 'MACHINE_ARCH' => 'aarch64'},
  'sh', '-s', '--', '-s', *DIAGNOSTIC_TARGETS, stdin_data: NATIVE_MAKEDEV)
abort diagnostic_error unless diagnostic_status.success?
NATIVE_DIAGNOSTIC_SPEC = diagnostic_output.lines.select { |line| line.start_with?('./') }.freeze
expected_paths = (DIAGNOSTIC_TARGETS + %w[dplcom1 dplcom3]).map { |name| './' + name }
abort 'Native diagnostic node inventory is incomplete' unless
  NATIVE_DIAGNOSTIC_SPEC.map { |line| line.split.first }.sort == expected_paths.sort
$checks = $failures = 0

def check(value, message)
  $checks += 1
  return if value
  $failures += 1
  warn "FAIL: #{message}"
end

def write(path, data, mode = 0o644)
  FileUtils.mkdir_p(File.dirname(path))
  File.binwrite(path, data)
  File.chmod(mode, path)
end

def command(env, *argv)
  out, err, status = Open3.capture3(env, *argv, unsetenv_others: true)
  raise "Fixture command failed: #{argv.first}: #{err}" unless status.success?
  out
end

def git_commit(env, path)
  command(env, 'git', '-C', path, 'init', '-q')
  command(env, 'git', '-C', path, 'add', '.')
  command(env, 'git', '-C', path, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
    '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'fixture')
  command(env, 'git', '-C', path, 'rev-parse', 'HEAD').strip
end

def manifest(f, paths = RUNTIME)
  rows = paths.map do |rel|
    path = File.join(f[:cohort], 'dest', rel)
    "#{Digest::SHA256.file(path).hexdigest}  #{path}\n"
  end
  File.binwrite(File.join(f[:cohort], 'BINARIES-SHA256.txt'), rows.join)
end

def fixture(base, name)
  dir = File.join(base, name)
  FileUtils.mkdir_p(dir)
  f = { dir: dir, cohort: dir + '/cohort', src: dir + '/source', tools: dir + '/tools',
        firmware: dir + '/firmware.bin', out: dir + '/output', log: dir + '/calls.log' }
  env = { 'PATH' => HOST_PATH, 'LC_ALL' => 'C', 'GIT_CONFIG_GLOBAL' => '/dev/null',
          'GIT_CONFIG_NOSYSTEM' => '1' }
  hooks = f[:src] + '/external/bsd/dhcpcd/dist/hooks/'
  write(hooks + 'dhcpcd-run-hooks', "#!/bin/sh\nexit 0\n", 0o755)
  write(hooks + '20-resolv.conf', "# synthetic DNS hook\n")
  write(f[:src] + '/sys/dev/ic/rtw88-firmware.LICENSE', "Synthetic firmware licence\n")
  write(f[:src] + '/external/realtek/rtw89/dist/LICENCE.rtlwifi_firmware.txt', "Synthetic firmware licence\n")
  write(f[:src] + '/etc/group', "wheel:*:0:root\noperator:*:5:root\ntty:*:4:\nkmem:*:2:\n")
  f[:revision] = git_commit(env, f[:src])
  profile = dir + '/profile'
  PROFILE_DATA.each { |file, bytes| write(profile + '/ember/image/' + file, bytes) }
  f[:profile] = profile
  f[:profile_revision] = git_commit(env, profile)
  f[:script] = profile + '/ember/image/cix-p1-root.sh'
  %w[bin sbin usr/bin usr/sbin usr/libexec].each { |p| FileUtils.mkdir_p(f[:cohort] + '/dest/' + p) }
  RUNTIME.each { |p| write(f[:cohort] + '/dest/' + p, "#!/bin/sh\nexit 0\n", 0o755) }
  f[:metalog] = f[:cohort] + '/dest/METALOG'
  special = { 'sbin/shutdown' => ['operator', '04554'], 'usr/bin/wall' => ['tty', '02555'],
              'usr/bin/netstat' => ['kmem', '02555'] }
  metadata = RUNTIME.map do |p|
    group, mode = special.fetch(p, ['wheel', '0755'])
    file = f[:cohort] + '/dest/' + p
    "./#{p} type=file uname=root gname=#{group} mode=#{mode} size=#{File.size(file)} time=1.0 sha256=#{Digest::SHA256.file(file).hexdigest}\n"
  end.join
  write(f[:metalog], metadata)
  write(f[:cohort] + '/BUILD-INPUTS.txt', "source=#{f[:revision]}\ngcc=16.2.0\ntarget=aarch64--netbsd\nprofile=reduced-static-c-development\n")
  manifest(f)
  spec_only = <<~SH
    #!/bin/sh
    case "$*" in
      '-s ramdisk wscons'|'-s ramdisk wscons sysmon'|'-s ramdisk wscons sysmon #{DIAGNOSTIC_TARGETS.join(' ')}') ;;
      *) echo 'Only fixture spec generation is permitted.' >&2; exit 92;;
    esac
  SH
  write(f[:cohort] + '/obj/etc/MAKEDEV', spec_only + NATIVE_MAKEDEV, 0o755)
  write(f[:firmware], "synthetic-approved-firmware\n")
  write(f[:cohort] + '/dest/etc/wpa_supplicant.conf', "SYNTHETIC_PROFILE_MUST_NOT_SHIP\n", 0o600)
  fake = dir + '/fakebin'
  FileUtils.mkdir_p(fake)
  tool = <<~'TOOL'
    #!/usr/bin/env ruby
    require 'digest'
    require 'json'
    require 'fileutils'
    name = File.basename($0)
    File.open(ENV.fetch('CALL_LOG'), 'a') { |f| f.puts(JSON.generate([name, *ARGV])) }
    case name
    when 'shasum'
      if ARGV == ['-a', '256', ENV.fetch('FIXTURE_FIRMWARE')]
        bytes = File.binread(ARGV.last)
        hash = bytes == "synthetic-approved-firmware\n" ? ENV.fetch('FIXTURE_FW_HASH') : Digest::SHA256.hexdigest(bytes)
        puts "#{hash}  #{ARGV.last}"
      else
        exec(ENV.fetch('REAL_SHASUM'), *ARGV)
      end
    when 'nbpwd_mkdb'
      abort 'unexpected password-db arguments' unless ARGV[0, 3] == ['-L', '-p', '-d']
      root = ARGV[3]
      abort 'unexpected password source' unless ARGV[4] == root + '/etc/master.passwd'
      File.write(root + '/etc/pwd.db', 'synthetic-public-db')
      File.write(root + '/etc/spwd.db', 'synthetic-protected-db')
    when 'nbmtree'
      abort 'unexpected mtree arguments' unless ARGV == ['-c', '-p', ENV.fetch('FIXTURE_OUT') + '/root', '-k', 'type,mode,link']
      puts '/set type=file mode=0755'
      puts '. type=dir mode=0755'
      puts './etc type=dir mode=0755'
      puts './etc/master.passwd type=file mode=0600'
    when 'nbmakefs'
      abort 'device argument passed to formatter' if ARGV.any? { |a| a.start_with?('/dev/') }
      out = ENV.fetch('FIXTURE_OUT')
      expected = ['-t', 'ffs', '-B', 'little', '-s', '2g', '-o', 'version=2,bsize=16384,fsize=2048',
        '-F', out + '/root.spec', '-N', ENV.fetch('FIXTURE_SOURCE') + '/etc', out + '/root.fs', out + '/root']
      abort 'unexpected image specification or destination' unless ARGV == expected
      File.write(ARGV[-2], 'synthetic-ffs-image')
      if ENV['MUTATE_TRACKED_ASSET']
        File.open(ENV.fetch('MUTATE_TRACKED_ASSET'), 'a') { |f| f.puts('# changed during image assembly') }
      end
    else
      abort 'forbidden media action: ' + name
    end
  TOOL
  %w[shasum dd diskutil mount newfs newfs_ffs gpt dkctl].each { |p| write(fake + '/' + p, tool, 0o755) }
  %w[nbmakefs nbmtree nbpwd_mkdb].each { |p| write(f[:tools] + '/bin/' + p, tool, 0o755) }
  f[:env] = env.merge('PATH' => fake + ':' + HOST_PATH, 'CALL_LOG' => f[:log], 'REAL_SHASUM' => REAL_SHASUM,
    'FIXTURE_FIRMWARE' => f[:firmware], 'FIXTURE_FW_HASH' => FW_HASH, 'FIXTURE_OUT' => f[:out],
    'FIXTURE_SOURCE' => f[:src])
  f
end

def run_stager(f)
  out, err, status = Open3.capture3(f[:env], 'sh', f[:script], f[:cohort], f[:src], f[:tools], f[:firmware], f[:out], unsetenv_others: true)
  calls = File.exist?(f[:log]) ? File.readlines(f[:log]).map { |s| JSON.parse(s) } : []
  [status.success?, out + err, calls]
end

def reject(f, label)
  success, output, calls = run_stager(f)
  check(!success, label + ': rejected')
  check(!File.exist?(f[:out]), label + ': output not created')
  check(calls.none? { |a| %w[nbmakefs nbmtree nbpwd_mkdb].include?(a.first) }, label + ': stopped before image tools')
  warn output if success
end

Dir.mktmpdir('ember-cix-root-image-') do |base|
  f = fixture(base, 'existing-output')
  write(f[:out] + '/sentinel', 'leave-existing-output-alone')
  before = Digest::SHA256.file(f[:out] + '/sentinel').hexdigest
  success, _, calls = run_stager(f)
  check(!success, 'existing output rejected')
  check(Dir.children(f[:out]) == ['sentinel'] && Digest::SHA256.file(f[:out] + '/sentinel').hexdigest == before, 'existing output unchanged')
  check(calls.empty?, 'existing output rejected before input tools')
  f = fixture(base, 'existing-output-link')
  File.symlink(f[:dir] + '/missing-target', f[:out])
  success, _, calls = run_stager(f)
  check(!success && File.symlink?(f[:out]), 'dangling output symlink rejected unchanged')
  check(calls.empty?, 'output symlink rejected before input tools')

  f = fixture(base, 'unverified-cohort')
  File.unlink(f[:cohort] + '/BUILD-INPUTS.txt')
  reject(f, 'unverified cohort')
  f = fixture(base, 'missing-binary-receipt')
  File.unlink(f[:cohort] + '/BINARIES-SHA256.txt')
  reject(f, 'missing binary receipt')
  f = fixture(base, 'mismatched-source')
  File.write(f[:cohort] + '/BUILD-INPUTS.txt', "source=#{'f' * 40}\n")
  reject(f, 'source revision mismatch')
  f = fixture(base, 'dirty-source')
  File.write(f[:src] + '/etc/group', 'modified after source receipt')
  reject(f, 'dirty tracked source')
  f = fixture(base, 'dirty-profile')
  File.open(f[:script], 'a') { |s| s.puts('# modified after profile commit') }
  reject(f, 'dirty tracked image profile')
  %w[sbin/fsck sbin/mount_tmpfs usr/sbin/wpa_supplicant usr/libexec/getty usr/sbin/powerd bin/stty usr/sbin/usbdevs].each_with_index do |rel, i|
    f = fixture(base, 'missing-runtime-' + i.to_s)
    File.unlink(f[:cohort] + '/dest/' + rel)
    manifest(f, RUNTIME - [rel])
    reject(f, 'missing runtime ' + rel)
  end
  f = fixture(base, 'binary-mismatch')
  File.open(f[:cohort] + '/dest/sbin/init', 'a') { |s| s.puts('changed after binary receipt') }
  reject(f, 'binary checksum mismatch')
  f = fixture(base, 'firmware-mismatch')
  File.write(f[:firmware], 'synthetic-corrupt-firmware')
  reject(f, 'firmware checksum mismatch')
  f = fixture(base, 'missing-image-tool')
  File.unlink(f[:tools] + '/bin/nbmakefs')
  reject(f, 'missing image tool')
  f = fixture(base, 'missing-device-spec')
  File.unlink(f[:cohort] + '/obj/etc/MAKEDEV')
  reject(f, 'missing device specification')
  f = fixture(base, 'partial-manifest')
  manifest(f, ['bin/sh'])
  reject(f, 'unverified binaries omitted from manifest')
  f = fixture(base, 'unlisted-runtime')
  write(f[:cohort] + '/dest/usr/bin/unverified', "#!/bin/sh\nexit 0\n", 0o755)
  reject(f, 'unverified extra executable')
  f = fixture(base, 'duplicate-manifest')
  File.open(f[:cohort] + '/BINARIES-SHA256.txt', 'a') do |s|
    s.write(File.readlines(f[:cohort] + '/BINARIES-SHA256.txt').first)
  end
  reject(f, 'duplicate binary receipt record')
  f = fixture(base, 'outside-manifest')
  write(f[:dir] + '/outside', 'not a selected runtime input')
  File.open(f[:cohort] + '/BINARIES-SHA256.txt', 'a') do |s|
    s.puts("#{Digest::SHA256.file(f[:dir] + '/outside').hexdigest}  #{f[:dir]}/outside")
  end
  reject(f, 'receipt path outside runtime inventory')
  f = fixture(base, 'runtime-symlink')
  File.symlink('stat', f[:cohort] + '/dest/usr/bin/unverified-alias')
  reject(f, 'runtime alias without a verified link receipt')
  f = fixture(base, 'missing-metadata')
  File.unlink(f[:metalog])
  reject(f, 'missing target metadata')
  f = fixture(base, 'missing-runtime-metadata')
  File.write(f[:metalog], File.readlines(f[:metalog]).reject { |s| s.start_with?('./sbin/shutdown ') }.join)
  reject(f, 'missing selected runtime metadata')

  ['source', 'profile'].each do |which|
    f = fixture(base, 'late-' + which + '-change')
    asset = which == 'source' ? f[:src] + '/etc/group' : f[:profile] + '/ember/image/cix-p1-network.subr'
    f[:env]['MUTATE_TRACKED_ASSET'] = asset
    success, _, _ = run_stager(f)
    check(!success, 'late ' + which + ' mutation rejects final acceptance')
    check(!File.exist?(f[:out] + '/ROOT-INPUTS.txt') && !File.exist?(f[:out] + '/ROOT-SHA256.txt'),
      'late ' + which + ' mutation emits no accepted image receipt')
  end

  f = fixture(base, 'success')
  success, output, calls = run_stager(f)
  check(success, 'verified synthetic cohort creates staged image')
  if success
    root = f[:out] + '/root'
    check(File.read(root + '/etc/fstab') == "NAME=ember-cix-root / ffs rw 1 1\n", 'expected stable root NAME')
    console = File.readlines(root + '/etc/ttys').find { |line| line.start_with?('console ') }
    check(console && Shellwords.split(console) == ['console', '/usr/libexec/getty ember-cix', 'vt100', 'on', 'secure'],
      'init starts getty with the selected profile as one command field')
    gettytab = root + '/etc/gettytab'
    entries = File.file?(gettytab) ? File.read(gettytab).gsub(/\\\n[\t ]*/, '').lines.map(&:strip) : []
    entry = entries.find { |line| !line.start_with?('#') && line.split(':').first.to_s.split('|').include?('ember-cix') }
    caps = entry ? entry.split(':') : []
    check(entry &&
      %w[al=root lo=/usr/libexec/ember-console sp#115200 np ce ck].all? { |cap| caps.include?(cap) },
      'getty creates the console session and selects the root wrapper without PAM/login')
    check(!File.exist?(root + '/etc/wpa_supplicant.conf'), 'template Wi-Fi profile is not shipped')
    check(File.file?(root + '/etc/powerd/scripts/power_button') && File.executable?(root + '/etc/powerd/scripts/power_button'),
      'the selected profile includes an executable power-button policy')
    check(Dir.glob(root + '/**/*').select { |p| !File.symlink?(p) && File.file?(p) }.none? { |p| File.binread(p).include?('SYNTHETIC_PROFILE_MUST_NOT_SHIP') }, 'synthetic profile data absent from all staged files')
    check((File.stat(root + '/root').mode & 0o7777) == 0o700 && (File.stat(root + '/tmp').mode & 0o7777) == 0o1777, 'root home and temporary directory modes')
    spec = File.read(f[:out] + '/root.spec')
    check(spec.include?("/set type=file mode=0755 uid=0 gid=0\n"), 'target ownership defaults are root')
    check(spec.lines.any? { |s| s.split.first == './dev/console' && s.include?('type=char') && s.include?('device=netbsd,2,0') && s.include?('mode=600') }, 'MAKEDEV nodes are rooted under dev')
    check(spec.lines.any? { |s| s.split.first == './dev/power' && s.include?('type=char') && s.include?('device=netbsd,101,2') && s.include?('mode=640') },
      'native AArch64 MAKEDEV provides the sysmon power character device')
    NATIVE_DIAGNOSTIC_SPEC.each do |native|
      expected = native.sub('./', './dev/')
      check(spec.lines.include?(expected), 'native diagnostic node specification retained: ' + expected.split.first)
    end
    device_paths = spec.lines.map { |line| line.split.first }
    check((device_paths.grep(%r{\A\./dev/usb\d*\z}) -
      ['usb', *(0..19).map { |i| "usb#{i}" }].map { |name| './dev/' + name }).empty?,
      'no USB controller beyond the selected diagnostic range')
    check(!device_paths.include?('./dev/plcom2') && !device_paths.include?('./dev/dplcom2'),
      'the DEBUG UART nodes are not staged')
    %w[bin/stty usr/sbin/usbdevs].each do |binary|
      check(File.executable?(root + '/' + binary), 'native diagnostic tool is staged: ' + binary)
    end
    check(!spec.include?("\n./console "), 'device spec does not create a root console path')
    { './sbin/shutdown' => ['operator', 0o4554], './usr/bin/wall' => ['tty', 0o2555],
      './usr/bin/netstat' => ['kmem', 0o2555] }.each do |path, (group, mode)|
      # The last explicit entry overrides host-generated ownership defaults.
      entry = spec.lines.select { |s| s.split.first == path }.last
      fields = entry ? entry.split.drop(1).map { |v| v.split('=', 2) }.to_h : {}
      check(fields['uname'] == 'root' && fields['gname'] == group && fields.fetch('mode', '0').to_i(8) == mode,
        'native target ownership/mode preserved for ' + path)
    end
    check(File.read(root + '/etc/group').include?("tty:*:4:\n") && File.read(root + '/etc/group').include?("kmem:*:2:\n"),
      'native group database retains runtime supplementary groups')
    check(File.file?(root + '/libdata/firmware/rtwce/LICENCE.rtlwifi_firmware.txt'), 'firmware licence is staged')
    check(File.read(f[:out] + '/ROOT-INPUTS.txt') == "userland=#{f[:revision]}\nprofile=#{f[:profile_revision]}\n", 'image receipt identifies userland and profile commits')
    makefs = calls.select { |a| a.first == 'nbmakefs' }
    check(makefs.size == 1 && makefs.first[makefs.first.index('-F') + 1] == f[:out] + '/root.spec', 'image format uses explicit target ownership/device spec')
    check(calls.none? { |a| %w[dd diskutil mount newfs newfs_ffs gpt dkctl].include?(a.first) }, 'no media action occurred')
  else
    warn output
  end
end
puts "CIX root image: #{$checks} checks, #{$failures} failures"
exit($failures.zero? ? 0 : 1)
