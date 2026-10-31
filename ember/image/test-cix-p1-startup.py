#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(os.environ.get('CIX_NETWORK_SOURCE', Path(__file__).with_name('cix-p1-network.subr')))
assert source.is_file(), 'missing network startup library'
checks = 0
shell = os.environ.get('TEST_SH')
if not shell or not os.path.isabs(shell):
    raise SystemExit('Set TEST_SH to the selected native shell')

with tempfile.TemporaryDirectory(prefix='cix-startup-') as name:
    tmp = Path(name)
    binary = tmp / 'bin'
    binary.mkdir()
    for command in ('ifconfig', 'wpa_supplicant', 'dhcpcd', 'wpa_cli'):
        path = binary / command
        path.write_text(f'''#!{shell}
printf '%s' '{command}' >> "$TRACE"
for arg do printf ' <%s>' "$arg" >> "$TRACE"; done
printf '\\n' >> "$TRACE"
case '{command}: '"$*" in "$FAIL"*) [ -z "$FAIL" ] || exit 9;; esac
if [ -n "${{CIX_STOP_PROBE:-}}" ]; then
  action=
  case '{command}: '"$*" in
    'ifconfig: rtwce0 down') action=down;;
    'ifconfig: rtwce0 up') action=up;;
    'dhcpcd: -k rtwce0'|'dhcpcd: -4 -k rtwce0') action=multicast;;
    wpa_cli:*) action=wpa;;
  esac
  if [ -n "$action" ]; then
    if [ -n "${{TEST_RUNNER:-}}" ]; then
      "$TEST_RUNNER" "$CIX_STOP_PROBE" "$CIX_STOP_STATE" "$action" || exit $?
    else
      "$CIX_STOP_PROBE" "$CIX_STOP_STATE" "$action" || exit $?
    fi
  fi
fi
exit 0
''')
        path.chmod(0o755)
    (binary / 'stat').write_text(f'#!{shell}\nprintf \'%s\\n\' "$PROFILE_MODE"\n')
    (binary / 'stat').chmod(0o755)
    profile = tmp / 'profile'
    profile.write_text('test profile: no credentials')
    trace, state = tmp / 'trace', tmp / 'state'

    def run(action, fail='', mode='0:600', config=profile):
        trace.write_text('')
        state.write_text('65 0\n')
        env = dict(os.environ, PATH=f'{binary}:{os.environ["PATH"]}', TRACE=str(trace),
                   FAIL=fail, PROFILE_MODE=mode, CIX_STOP_STATE=str(state))
        body = f'. "$1"\ncix_run="$2/run"\ncix_log="$2/log"\ncix_network_{action} rtwce0 "$3"\n'
        result = subprocess.run([shell, '-c', body, 'contract', str(source), str(tmp), str(config)],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return result.returncode == 0, trace.read_text(), result.stdout

    def check(condition, message):
        global checks
        checks += 1
        assert condition, message

    ok, text, _ = run('start')
    check(ok, 'valid start succeeds')
    lines = text.splitlines(keepends=True)
    check(lines[0] == 'ifconfig <rtwce0> <down>\n', 'first take interface down')
    check(lines[1] == 'ifconfig <rtwce0> <roaming> <manual>\n', 'set manual before up')
    check(lines[2] == 'ifconfig <rtwce0> <up>\n', 'then bring interface up')
    check(lines[3].startswith('wpa_supplicant <-B> <-Dbsd> <-i> <rtwce0>'), 'one background WPA process')
    check(lines[4] == 'dhcpcd <-b> <-C> <wpa_supplicant> <rtwce0>\n', 'background dual-stack DHCP')
    check(len(lines) == 5, 'no synchronous association poll')
    check((tmp / 'log/wpa_supplicant.log').stat().st_mode & 0o777 == 0o600, 'private WPA log')
    for failure in ('ifconfig: rtwce0 down', 'ifconfig: rtwce0 roaming manual',
                    'ifconfig: rtwce0 up', 'wpa_supplicant:'):
        ok, text, _ = run('start', failure)
        check(not ok and 'dhcpcd' not in text, f'failure stops startup: {failure}')
    ok, text, _ = run('start', 'dhcpcd:')
    check(not ok and 'wpa_cli' in text, 'DHCP launch failure tears down WPA')
    check(text.splitlines(keepends=True)[-2] == 'ifconfig <rtwce0> <down>\n', 'DHCP failure takes interface down')
    for mode in ('1000:600', '0:644', '0:400', ''):
        ok, text, _ = run('start', mode=mode)
        check(not ok and not text, f'reject unsafe profile ownership/mode {mode}')
    ok, text, _ = run('start', config=str(profile) + '.missing')
    check(not ok and not text, 'missing config cannot change interface')
    link = tmp / 'profile.link'
    link.symlink_to(profile)
    ok, text, _ = run('start', config=link)
    check(not ok and not text, 'reject symlink config')
    ok, text, _ = run('stop')
    if os.environ.get('CIX_STOP_PROBE'):
        check(state.read_text().split() == ['0', '0'], 'cleanup must not restart an UP radio')
    lines = text.splitlines(keepends=True)
    check(ok and lines[0] == 'ifconfig <rtwce0> <down>\n', 'quiesce radio before address cleanup')
    check(lines[1] == 'dhcpcd <-k> <rtwce0>\n', 'stop dual-stack daemon after DOWN')
    check(lines[2] == 'dhcpcd <-4> <-k> <rtwce0>\n', 'stop legacy IPv4 daemon')
    check(lines[3].startswith('wpa_cli <-p>') and '<terminate>' in text, 'stop owned WPA')
    ok, text, _ = run('stop', 'dhcpcd:')
    check(ok and 'wpa_cli' in text, 'absent DHCP does not prevent WPA stop')
    if os.environ.get('CIX_STOP_PROBE'):
        check(state.read_text().split() == ['0', '0'], 'DHCP failure leaves radio stopped')
    ok, text, _ = run('stop', 'dhcpcd: -4 -k')
    check(ok and text.splitlines(keepends=True)[1] == 'dhcpcd <-k> <rtwce0>\n' and 'wpa_cli' in text,
          'absent legacy IPv4 daemon retains cleanup')
    ok, text, _ = run('stop', 'wpa_cli:')
    check(ok and 'dhcpcd' in text, 'WPA control failure retains DHCP cleanup')
    if os.environ.get('CIX_STOP_PROBE'):
        check(state.read_text().split() == ['0', '0'], 'WPA failure leaves radio stopped')
    ok, text, _ = run('stop', 'ifconfig: rtwce0 down')
    check(not ok and len(text.splitlines()) == 1, 'failed DOWN cannot clean DHCP or restore WPA while UP')
    if os.environ.get('CIX_STOP_PROBE'):
        check(state.read_text().split() == ['65', '0'], 'failed DOWN preserves flags without cleanup restart')

print(f'CIX network startup: {checks} checks, 0 failures')
