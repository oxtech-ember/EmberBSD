#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Run the actual opt-in startup with daemon, stat and mount boundaries.
require 'tmpdir'
require 'fileutils'
require 'open3'

source = File.expand_path('cix-p1-ssh.subr', __dir__)
checks = 0
check = lambda do |value, message|
  checks += 1
  abort "FAIL: #{message}" unless value
end
Dir.mktmpdir('cix-ssh-') do |tmp|
  %w[bin sbin libexec etc/ssh run pts].each { |p| FileUtils.mkdir_p(tmp + '/' + p) }
  trace = tmp + '/trace'
  config = tmp + '/etc/ssh/sshd_config'
  File.write(config, "# synthetic administrator configuration, no secrets\n")
  File.chmod(0o600, config)
  File.write(tmp + '/bin/stat', <<~'SH')
    #!/bin/sh
    case "$3" in */sshd_config) echo "$CONFIG_MODE";; *) echo "$DIRECTORY_MODE";; esac
  SH
  File.write(tmp + '/sbin/sshd', <<~'SH')
    #!/bin/sh
    printf 'sshd %s\n' "$*" >> "$TRACE"
    case "$1" in -t) [ "$FAIL" != config ];; *) [ "$FAIL" != daemon ];; esac
  SH
  File.write(tmp + '/bin/mount', <<~'SH')
    #!/bin/sh
    [ "$#" = 0 ] || { echo "native mount rejects options" >&2; exit 91; }
    printf 'mount\n' >> "$TRACE"
    [ "$FAIL" != mount-list ] || exit 9
    printf '%s\n' "$MOUNTS"
  SH
  File.write(tmp + '/bin/mount_ptyfs', <<~'SH')
    #!/bin/sh
    printf 'mount_ptyfs %s\n' "$*" >> "$TRACE"
    [ "$FAIL" != mount ]
  SH
  %w[sshd-session sshd-auth].each { |p| File.write(tmp + '/libexec/' + p, "#!/bin/sh\nexit 91\n") }
  Dir[tmp + '/bin/*', tmp + '/sbin/*', tmp + '/libexec/*'].each { |p| File.chmod(0o755, p) }
  run = lambda do |failure = '', mode = '0:600', mounts = '', directory = '0:755', twice = false|
    File.write(trace, '')
    before = File.file?(config) ? File.binread(config) : nil
    env = { 'PATH' => tmp + '/bin:/usr/bin:/bin', 'TRACE' => trace,
      'FAIL' => failure, 'CONFIG_MODE' => mode, 'DIRECTORY_MODE' => directory, 'MOUNTS' => mounts }
    body = <<~SH
      . "$1" || exit 99
      cix_ssh_etc="$2/etc/ssh"
      cix_ssh_sbin="$2/sbin"
      cix_ssh_libexec="$2/libexec"
      cix_ssh_run="$2/run"
      cix_ssh_pts="$2/pts"
      cix_ssh_start || exit $?
      #{twice ? 'cix_ssh_start' : ':'}
    SH
    output, status = Open3.capture2e(env, ENV.fetch('TEST_SH', 'sh'), '-c', body, 'contract', source, tmp)
    check.call(before == (File.file?(config) ? File.binread(config) : nil), 'administrator config is unchanged')
    [status.success?, File.read(trace).lines, output]
  end
  ok, events, = run.call
  check.call(ok, 'valid opt-in starts SSH')
  check.call(events == ["sshd -t -f #{config}\n", "mount\n",
    "mount_ptyfs ptyfs #{tmp}/pts\n", "sshd -f #{config}\n"], 'validate before PTY mount and daemon launch')
  File.rename(config, config + '.saved')
  ok, events, = run.call
  check.call(ok && events.empty?, 'absent config disables SSH without mount or daemon effects')
  check.call(!File.exist?(config), 'disabled startup creates no config')
  File.symlink(config + '.saved', config)
  ok, events, = run.call
  check.call(!ok && events.empty?, 'symlink config cannot enable SSH')
  File.unlink(config)
  File.symlink(config + '.missing', config)
  ok, events, = run.call
  check.call(!ok && events.empty?, 'dangling config symlink is rejected')
  File.unlink(config)
  File.rename(config + '.saved', config)
  %w[1000:600 0:644 0:400 0:1600].each do |mode|
    ok, events, = run.call('', mode)
    check.call(!ok && events.empty?, 'unsafe config metadata is rejected: ' + mode)
  end
  %w[1000:755 0:777].each do |mode|
    ok, events, = run.call('', '0:600', '', mode)
    check.call(!ok && events.empty?, 'unsafe SSH directory is rejected: ' + mode)
  end
  %w[sshd-session sshd-auth].each do |helper|
    path = tmp + '/libexec/' + helper
    File.chmod(0o644, path)
    ok, events, = run.call
    check.call(!ok && events.empty?, 'missing executable helper stops before config test: ' + helper)
    File.chmod(0o755, path)
  end
  File.write(tmp + '/run/sshd.pid', 'synthetic existing instance')
  ok, events, = run.call
  check.call(!ok && events.empty?, 'existing PID file rejects duplicate daemon')
  File.unlink(tmp + '/run/sshd.pid')
  ok, events, = run.call('config')
  check.call(!ok && events.size == 1, 'bad config/account/key stops before mounts or launch')
  ok, events, = run.call('mount-list')
  check.call(!ok && events.size == 2, 'mount inventory failure stops launch')
  ok, events, = run.call('mount')
  check.call(!ok && events.size == 3, 'PTY mount failure stops launch')
  ok, events, = run.call('daemon')
  check.call(!ok && events.size == 4, 'daemon failure is visible to caller')
  ok, events, = run.call('', '0:600', "ptyfs on #{tmp}/pts type ptyfs (local)")
  check.call(ok && events.none? { |e| e.start_with?('mount_ptyfs') }, 'existing ptyfs is not mounted twice')
  ok, events, = run.call('', '0:600', "tmpfs on #{tmp}/pts type tmpfs (local)")
  check.call(!ok && events.size == 2, 'conflicting mount is not covered or used')
ok, events, = run.call('', '0:600', "/dev/dk1 on / type ffs (local)\ntmpfs on /var/run type tmpfs (local)\nptyfs on #{tmp}/pts type ptyfs")
check.call(ok && events.none? { |e| e.start_with?('mount_ptyfs') }, 'native mount output supports flags or no flags')
ok, events, = run.call('', '0:600', "ptyfs on #{tmp}/pts.other type ptyfs (local)")
check.call(ok && events.any? { |e| e.start_with?('mount_ptyfs') }, 'mountpoint prefix cannot hide the missing exact mount')
["ptyfs on #{tmp}/pts type ptyfs\nptyfs on #{tmp}/pts type ptyfs",
 "ptyfs #{tmp}/pts ptyfs rw 0 0", "ptyfs on #{tmp}/pts nonsense ptyfs"].each do |listing|
  ok, events, = run.call('', '0:600', listing)
  check.call(!ok && events.size == 2, 'duplicate or unrecognized listing is rejected before mount/launch')
end
  ok, events, = run.call('', '0:600', '', '0:755', true)
  check.call(!ok && events.size == 4, 'same startup context cannot launch twice')
# Execute the real rc flow. Redirect filesystem/command boundaries only.
File.write(tmp + '/etc/cix-root.subr', <<~'SH')
  cix_root_start() { echo root >> "$TRACE"; [ "$ROOT_FAIL" != yes ]; }
SH
File.write(tmp + '/etc/cix-network.subr', <<~'SH')
  cix_network_start() { echo network >> "$TRACE"; return 9; }
SH
File.write(tmp + '/etc/cix-power.subr', <<~'SH')
  cix_power_start() { echo power >> "$TRACE"; return 0; }
SH
FileUtils.cp(source, tmp + '/etc/cix-ssh.subr')
%w[mkdir chmod hostname ifconfig rndctl].each do |command|
  File.write(tmp + '/bin/' + command, "#!/bin/sh\nprintf '%s %s\\n' '#{command}' \"$*\" >> \"$TRACE\"\n")
  File.chmod(0o755, tmp + '/bin/' + command)
end
rc = File.binread(ENV.fetch('CIX_RC_SOURCE', File.expand_path('cix-p1-rc', __dir__)))
rc = rc.gsub(%r{/(?:etc|var)/}) { |prefix| tmp + prefix }
  .sub('PATH=/sbin:/bin:/usr/sbin:/usr/bin', "PATH=#{tmp}/bin:/usr/bin:/bin")
abort 'Unsafe rc fixture PATH' unless rc.lines.include?("PATH=#{tmp}/bin:/usr/bin:/bin\n")
File.write(tmp + '/rc', rc)
rc_run = lambda do |root_failure = '', ssh_failure = ''|
  File.write(trace, '')
  env = { 'TRACE' => trace, 'ROOT_FAIL' => root_failure, 'FAIL' => ssh_failure,
    'CONFIG_MODE' => '0:600', 'DIRECTORY_MODE' => '0:755', 'MOUNTS' => '',
    'cix_ssh_etc' => tmp + '/etc/ssh', 'cix_ssh_sbin' => tmp + '/sbin',
    'cix_ssh_libexec' => tmp + '/libexec', 'cix_ssh_run' => tmp + '/run', 'cix_ssh_pts' => tmp + '/pts' }
  output, status = Open3.capture2e(env, ENV.fetch('TEST_SH', 'sh'), tmp + '/rc')
  [status.success?, File.read(trace).lines, output]
end
ok, events, output = rc_run.call
ssh = events.index("sshd -f #{config}\n")
entropy = events.index { |e| e.start_with?('rndctl -S ') }
network = events.index("network\n")
check.call(ok && ssh && entropy && network && entropy < ssh && ssh < network,
  'actual rc starts SSH after entropy and before failing network startup: ' + [events, output].inspect)
check.call(output.include?('WARNING: Wi-Fi startup failed.'), 'network failure remains visible')
ok, events, = rc_run.call('yes')
check.call(!ok && events == ["root\n"], 'failed root check cannot start SSH or networking')
ok, events, output = rc_run.call('', 'daemon')
check.call(ok && events.include?("network\n") && output.include?('WARNING: SSH startup failed.'),
  'SSH failure warns without preventing local-console/network startup')
  check.call(Dir.children(tmp + '/etc/ssh') == ['sshd_config'], 'no host keys or default config are generated')
end
puts "CIX SSH startup: #{checks} checks, 0 failures"
