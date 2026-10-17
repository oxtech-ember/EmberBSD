#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Execute the profile policy with an explicit shutdown boundary double.
# Never execute host shutdown, powerd, or a real power device.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'

here = __dir__
shell = ENV.fetch('TEST_SH', 'sh')
library = File.join(here, 'cix-p1-power.subr')
policy = File.join(here, 'cix-p1-power-button')
abort 'FAIL: missing CIX power startup library' unless File.file?(library)
abort 'FAIL: missing CIX power-button policy' unless File.file?(policy)
checks = 0
check = lambda do |value, message|
  checks += 1
  abort "FAIL: #{message}" unless value
end
Dir.mktmpdir('cix-power-') do |tmp|
  bin = tmp + '/bin'
  FileUtils.mkdir_p(bin)
  trace = tmp + '/trace'
  File.write(bin + '/powerd', <<~SH)
    #!/bin/sh
    printf '%s\\n' "$*" >> "$TRACE"
    if [ "$FAIL_POWERD" = yes ]; then echo 'synthetic power open failure' >&2; exit 9; fi
    exec /bin/sleep 30
  SH
  File.write(bin + '/sleep', <<~SH)
    #!/bin/sh
    [ "$*" = 1 ] || exit 91
    n=0
    while [ ! -s "$TRACE" ] && [ "$n" -lt 50 ]; do
      /bin/sleep 0.01
      n=$((n + 1))
    done
    /bin/sleep 0.05
  SH
  File.write(bin + '/shutdown-double', <<~SH)
    #!/bin/sh
    printf 'shutdown' >> "$TRACE"
    for arg do printf ' <%s>' "$arg" >> "$TRACE"; done
    printf '\\n' >> "$TRACE"
    [ "$FAIL_SHUTDOWN" != yes ]
  SH
  # This reduced cohort has rm -d, but no rmdir executable.
  File.write(bin + '/rmdir', "#!/bin/sh\necho 'rmdir is absent from the reduced cohort' >&2\nexit 127\n")
  Dir.glob(bin + '/*').each { |p| File.chmod(0o755, p) }
  %w[hostname ifconfig rndctl sync].each do |name|
    File.write(bin + '/' + name, "#!/bin/sh\nexit 0\n")
    File.chmod(0o755, bin + '/' + name)
  end
  env = {'PATH' => bin + ':/usr/bin:/bin', 'TRACE' => trace}
  # Only the /dev/power filesystem probe is replaced on non-NetBSD hosts.
  probe = <<~'SH'
    test() {
      if [ "$#" -eq 2 ] && [ "$1" = -c ] && [ "$2" = /dev/power ]; then
        command test -c "$POWER_DEVICE"
      else
        command test "$@"
      fi
    }
  SH
  run_start = lambda do |name, extra = {}|
    dir = tmp + '/' + name
    FileUtils.mkdir_p(dir + '/run')
    File.write(trace, '')
    script = probe + <<~SH
      . "$1"
      cix_run="$2/run"
      cix_log="$2/log"
      printf() {
        if [ "$FAIL_PID_WRITE" = yes ] && [ "$#" -eq 2 ]; then return 1; fi
        command printf "$@"
      }
      cix_power_start
    SH
    output, status = Open3.capture2e(env.merge('POWER_DEVICE' => '/dev/null').merge(extra), shell, '-c', script,
      'power-contract', library, dir)
    [status.success?, output, dir]
  end
  live = []
  begin
    ok, _, dir = run_start.call('success')
    pidfile = dir + '/run/cix-powerd/pid'
    live << File.read(pidfile).to_i if File.file?(pidfile)
    check.call(ok, 'native foreground powerd launches under the profile')
    check.call(File.read(trace) == "-d\n", 'debug mode retains stderr without syslogd')
    check.call(File.file?(pidfile) && live.last.to_i > 1, 'record the launched child PID')
    check.call(File.stat(dir + '/log/powerd.log').mode & 0o777 == 0o600, 'private daemon log')
    ok, output, = run_start.call('success')
    check.call(!ok && File.read(trace).empty?, 'duplicate startup cannot launch another daemon')
    check.call(output.include?('powerd'), 'duplicate startup is visible')
    ok, output, dir = run_start.call('failed-child', 'FAIL_POWERD' => 'yes')
    check.call(!ok && output.include?('synthetic power open failure'), 'child startup failure reaches the console')
    check.call(File.read(dir + '/log/powerd.log').include?('synthetic power open failure'), 'child error retained in private log')
    check.call(!File.exist?(dir + '/run/cix-powerd'), 'failed startup releases its startup guard')
    ok, _, dir = run_start.call('failed-pid-write', 'FAIL_PID_WRITE' => 'yes')
    check.call(!ok && !File.exist?(dir + '/run/cix-powerd'), 'failed PID write removes the redirected empty PID file and startup guard')
    ok, _, dir = run_start.call('failed-pid-write')
    pidfile = dir + '/run/cix-powerd/pid'
    live << File.read(pidfile).to_i if File.file?(pidfile)
    check.call(ok && File.file?(pidfile), 'failed PID write permits a later startup retry')
    ok, _, dir = run_start.call('missing-device', 'POWER_DEVICE' => tmp + '/no-device')
    check.call(!ok && File.read(trace).empty?, 'missing character device cannot launch powerd')
    File.write(tmp + '/regular-device', 'not a character device')
    ok, _, = run_start.call('bad-regular-device', 'POWER_DEVICE' => tmp + '/regular-device')
    check.call(!ok && File.read(trace).empty?, 'regular device impostor cannot launch powerd')
    FileUtils.mkdir_p(tmp + '/bad-log/log/powerd.log')
    ok, _, dir = run_start.call('bad-log')
    check.call(!ok && File.read(trace).empty? && !File.exist?(dir + '/run/cix-powerd'), 'log-open failure cannot launch an unobservable daemon')
    File.rename(bin + '/powerd', bin + '/powerd.saved')
    begin
      ok, output, dir = run_start.call('missing-program')
      check.call(!ok && output.include?('powerd') && !File.exist?(dir + '/run/cix-powerd'), 'missing program reports failure and releases the startup guard')
    ensure
      File.rename(bin + '/powerd.saved', bin + '/powerd')
    end

    # Execute the actual rc with an isolated root and external command doubles.
    # Root checking and networking have their own source contracts.
    rcroot = tmp + '/rc-root'
    FileUtils.mkdir_p([rcroot + '/etc', rcroot + '/var/run', rcroot + '/var/db'])
    File.write(rcroot + '/etc/cix-root.subr', "cix_root_start() { echo root >> \"$TRACE\"; }\n")
    File.write(rcroot + '/etc/cix-network.subr', "cix_network_start() { echo network >> \"$TRACE\"; }\n")
    FileUtils.cp(library, rcroot + '/etc/cix-power.subr')
    rc = File.read(ENV.fetch('RC_SCRIPT', File.join(here, 'cix-p1-rc')))
      .gsub(%r{/(etc|var)/}) { rcroot + '/' + Regexp.last_match(1) + '/' }
      .sub(/^PATH=.*$/, 'PATH=' + Shellwords.escape(env.fetch('PATH')))
    rcpath = tmp + '/rc'
    File.write(rcpath, rc)
    rcenv = env.merge('cix_run' => rcroot + '/var/run', 'cix_log' => rcroot + '/var/log',
      'POWER_DEVICE' => '/dev/null')
    File.write(trace, '')
    output, status = Open3.capture2e(rcenv, shell, '-c', probe + '. "$1"', 'rc-contract', rcpath)
    rcpid = rcroot + '/var/run/cix-powerd/pid'
    live << File.read(rcpid).to_i if File.file?(rcpid)
    check.call(status.success? && File.file?(rcpid), 'actual rc starts the native power daemon')
    check.call(File.read(trace).lines.map(&:strip) == %w[root -d network], 'power startup follows writable root and precedes network launch')
    check.call(output.include?('background'), 'console still reports asynchronous network startup')
    FileUtils.rm_rf(rcroot + '/var/run/cix-powerd')
    File.write(trace, '')
    output, status = Open3.capture2e(rcenv.merge('FAIL_POWERD' => 'yes'), shell, '-c', probe + '. "$1"', 'rc-contract', rcpath)
    check.call(status.success? && output.include?('WARNING: powerd startup failed'), 'actual rc exposes failure and preserves the console')
    check.call(File.read(trace).include?('network'), 'power startup failure does not suppress Wi-Fi startup')

    # Replace only the privileged command and volatile state path in a copy.
    # Abort before execution if the expected command boundary is absent.
    text = File.read(policy)
    check.call(text.scan('/sbin/shutdown').size == 1, 'one explicit shutdown boundary can be isolated')
    action = tmp + '/power_button'
    lock = tmp + '/poweroff.pending'
    File.write(action, text.sub('/sbin/shutdown', Shellwords.escape(bin + '/shutdown-double'))
      .gsub('/var/run/cix-poweroff.pending', lock))
    invoke = lambda do |event, extra = {}|
      Open3.capture2e(env.merge(extra), shell, action, 'acpibut0', event)
    end
    File.write(trace, '')
    _, status = invoke.call('released')
    check.call(status.success? && File.read(trace).empty? && !File.exist?(lock), 'release does not request shutdown')
    _, status = invoke.call('unknown')
    check.call(!status.success? && File.read(trace).empty?, 'unsupported event cannot request shutdown')
    _, status = invoke.call('pressed')
    check.call(status.success? && File.read(trace) == "shutdown <-p> <now> <power button pressed>\n", 'press requests the orderly poweroff path')
    check.call(File.directory?(lock), 'accepted request keeps the duplicate guard')
    _, status = invoke.call('pressed')
    check.call(status.success? && File.readlines(trace).size == 1, 'duplicate press does not repeat shutdown')
    FileUtils.rm_rf(lock)
    File.write(trace, '')
    _, status = invoke.call('pressed', 'FAIL_SHUTDOWN' => 'yes')
    check.call(!status.success? && !File.exist?(lock), 'synchronous shutdown failure releases the guard')
    _, status = invoke.call('pressed')
    check.call(status.success? && File.readlines(trace).size == 2, 'failed request permits a later retry')
    FileUtils.rm_rf(lock)
    File.write(trace, '')
    threads = 4.times.map { Thread.new { invoke.call('pressed') } }
    check.call(threads.map(&:value).all? { |_, s| s.success? } && File.readlines(trace).size == 1,
      'concurrent presses admit exactly one shutdown request')
  ensure
    live.each { |pid| Process.kill('TERM', pid) rescue Errno::ESRCH }
  end
end
puts "CIX power policy: #{checks} checks, 0 failures"
