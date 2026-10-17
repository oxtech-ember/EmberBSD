#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'

source = ENV.fetch('CIX_NETWORK_SOURCE', File.expand_path('cix-p1-network.subr', __dir__))
abort 'missing network startup library' unless File.file?(source)
checks = 0
Dir.mktmpdir('cix-startup-') do |tmp|
  bin = File.join(tmp, 'bin')
  FileUtils.mkdir_p(bin)
  %w[ifconfig wpa_supplicant dhcpcd wpa_cli].each do |command|
    File.write(File.join(bin, command), <<~SH)
      #!/bin/sh
      printf '%s' '#{command}' >> "$TRACE"
      for arg do printf ' <%s>' "$arg" >> "$TRACE"; done
      printf '\n' >> "$TRACE"
      case '#{command}: '"$*" in "$FAIL"*) [ -z "$FAIL" ] || exit 9;; esac
      if [ -n "${CIX_STOP_PROBE:-}" ]; then
        action=
        case '#{command}: '"$*" in
          'ifconfig: rtwce0 down') action=down;;
          'ifconfig: rtwce0 up') action=up;;
          'dhcpcd: -4 -k rtwce0') action=multicast;;
          wpa_cli:*) action=wpa;;
        esac
        if [ -n "$action" ]; then
          if [ -n "${TEST_RUNNER:-}" ]; then
            "$TEST_RUNNER" "$CIX_STOP_PROBE" "$CIX_STOP_STATE" "$action" || exit $?
          else
            "$CIX_STOP_PROBE" "$CIX_STOP_STATE" "$action" || exit $?
          fi
        fi
      fi
      exit 0
    SH
    FileUtils.chmod(0755, File.join(bin, command))
  end
  # BSD stat is a filesystem-boundary fake on non-NetBSD hosts.
  File.write(File.join(bin, 'stat'), "#!/bin/sh\nprintf '%s\\n' \"$PROFILE_MODE\"\n")
  FileUtils.chmod(0755, File.join(bin, 'stat'))
  profile = File.join(tmp, 'profile')
  File.write(profile, 'test profile: no credentials')
  trace = File.join(tmp, 'trace')
  state = File.join(tmp, 'state')
  run = lambda do |action, fail = '', mode = '0:600', config = profile|
    File.write(trace, '')
    # Start UP|RUNNING: the native ioctl decides whether cleanup restarts it.
    File.write(state, "65 0\n")
    env = {'PATH' => "#{bin}:/usr/bin:/bin", 'TRACE' => trace,
           'FAIL' => fail, 'PROFILE_MODE' => mode, 'CIX_STOP_STATE' => state}
    body = <<~SH
      . "$1"
      cix_run="$2/run"
      cix_log="$2/log"
      cix_network_#{action} rtwce0 "$3"
    SH
    output, status = Open3.capture2e(env, ENV.fetch('TEST_SH', 'sh'), '-c', body, 'contract', source, tmp, config)
    [status.success?, File.read(trace), output]
  end
  assert = lambda do |condition, message|
    checks += 1
    abort "FAIL: #{message}" unless condition
  end
  ok, trace_text, = run.call('start')
  assert.call(ok, 'valid start succeeds')
  lines = trace_text.lines
  assert.call(lines[0] == "ifconfig <rtwce0> <down>\n", 'first take interface down')
  assert.call(lines[1] == "ifconfig <rtwce0> <roaming> <manual>\n", 'set manual before up')
  assert.call(lines[2] == "ifconfig <rtwce0> <up>\n", 'then bring interface up')
  assert.call(lines[3]&.start_with?('wpa_supplicant <-B> <-Dbsd> <-i> <rtwce0>'), 'one background WPA process')
  assert.call(lines[4] == "dhcpcd <-b> <-4> <-C> <wpa_supplicant> <rtwce0>\n", 'background IPv4 DHCP and disable duplicate WPA hook')
  assert.call(lines.size == 5, 'no synchronous association poll')
  assert.call(File.stat(File.join(tmp, 'log', 'wpa_supplicant.log')).mode & 0777 == 0600, 'private WPA log')
  %w[ifconfig:\ rtwce0\ down ifconfig:\ rtwce0\ roaming\ manual ifconfig:\ rtwce0\ up wpa_supplicant:].each do |failure|
    ok, text, = run.call('start', failure)
    assert.call(!ok && !text.include?('dhcpcd'), "failure stops startup: #{failure}")
  end
  ok, text, = run.call('start', 'dhcpcd:')
  assert.call(!ok && text.include?('wpa_cli'), 'DHCP launch failure tears down WPA')
  assert.call(text.lines[-2] == "ifconfig <rtwce0> <down>\n", 'DHCP failure takes interface down before WPA restores AUTO')
  ['1000:600', '0:644', '0:400', ''].each do |mode|
    ok, text, = run.call('start', '', mode)
    assert.call(!ok && text.empty?, "reject unsafe profile ownership/mode #{mode}")
  end
  ok, text, = run.call('start', '', '0:600', profile + '.missing')
  assert.call(!ok && text.empty?, 'missing config cannot change interface')
  File.symlink(profile, profile + '.link')
  ok, text, = run.call('start', '', '0:600', profile + '.link')
  assert.call(!ok && text.empty?, 'reject symlink config')
  ok, text, = run.call('stop')
  if ENV['CIX_STOP_PROBE']
    assert.call(File.read(state).split.map(&:to_i) == [0, 0], "cleanup must not restart an UP radio: #{File.read(state).strip}")
  end
  assert.call(ok && text.lines.first == "ifconfig <rtwce0> <down>\n", 'quiesce radio before address cleanup')
  assert.call(text.lines[1] == "dhcpcd <-4> <-k> <rtwce0>\n", 'stop correct IPv4 daemon after DOWN')
  assert.call(text.lines[2]&.start_with?('wpa_cli <-p>') && text.include?('<terminate>'), 'stop the owned WPA instance')
  ok, text, = run.call('stop', 'dhcpcd:')
  assert.call(ok && text.include?('wpa_cli'), 'absent DHCP does not prevent WPA stop')
  if ENV['CIX_STOP_PROBE']
    assert.call(File.read(state).split.map(&:to_i) == [0, 0], 'DHCP failure leaves radio stopped')
  end
  ok, text, = run.call('stop', 'wpa_cli:')
  assert.call(ok && text.include?('dhcpcd'), 'WPA control failure retains completed DHCP cleanup')
  if ENV['CIX_STOP_PROBE']
    assert.call(File.read(state).split.map(&:to_i) == [0, 0], 'WPA failure leaves radio stopped')
  end
  ok, text, = run.call('stop', 'ifconfig: rtwce0 down')
  assert.call(!ok && text.lines.size == 1, 'failed DOWN cannot clean DHCP or restore WPA while UP')
  if ENV['CIX_STOP_PROBE']
    assert.call(File.read(state).split.map(&:to_i) == [65, 0], 'failed DOWN preserves flags without cleanup restart')
  end
end
puts "CIX network startup: #{checks} checks, 0 failures"
