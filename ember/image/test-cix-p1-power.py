#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise power policy using controlled shutdown and device boundaries."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import tempfile

here = Path(__file__).parent
shell = os.environ.get('TEST_SH')
if not shell or not os.path.isabs(shell):
    raise SystemExit('Set TEST_SH to the selected native shell')
real_sleep = shutil.which('sleep')
assert real_sleep, 'selected sleep missing'
library, policy = here / 'cix-p1-power.subr', here / 'cix-p1-power-button'
assert library.is_file() and policy.is_file(), 'missing CIX power policy'
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


with tempfile.TemporaryDirectory(prefix='cix-power-') as name:
    tmp = Path(name)
    binary = tmp / 'bin'
    binary.mkdir()
    trace = tmp / 'trace'
    scripts = {
        'powerd': f'''printf '%s\\n' "$*" >> "$TRACE"
if [ "$FAIL_POWERD" = yes ]; then echo 'synthetic power open failure' >&2; exit 9; fi
exec {shlex.quote(real_sleep)} 30
''',
        'sleep': f'''[ "$*" = 1 ] || exit 91
n=0
while [ ! -s "$TRACE" ] && [ "$n" -lt 50 ]; do
  {shlex.quote(real_sleep)} 0.01
  n=$((n + 1))
done
{shlex.quote(real_sleep)} 0.05
''',
        'shutdown-double': '''printf 'shutdown' >> "$TRACE"
for arg do printf ' <%s>' "$arg" >> "$TRACE"; done
printf '\\n' >> "$TRACE"
[ "$FAIL_SHUTDOWN" != yes ]
''',
        'rmdir': "echo 'rmdir is absent from the reduced cohort' >&2\nexit 127\n",
        'hostname': 'exit 0\n', 'ifconfig': 'exit 0\n', 'rndctl': 'exit 0\n', 'sync': 'exit 0\n',
    }
    for file, body in scripts.items():
        (binary / file).write_text(f'#!{shell}\n' + body)
        (binary / file).chmod(0o755)
    env = dict(os.environ, PATH=f'{binary}:{os.environ["PATH"]}', TRACE=str(trace))
    probe = '''test() {
  if [ "$#" -eq 2 ] && [ "$1" = -c ] && [ "$2" = /dev/power ]; then
    command test -c "$POWER_DEVICE"
  else
    command test "$@"
  fi
}
'''

    def execute(argv, extra=None):
        return subprocess.run(argv, env=dict(env, **(extra or {})), stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)

    def run_start(name, extra=None):
        directory = tmp / name
        (directory / 'run').mkdir(parents=True, exist_ok=True)
        trace.write_text('')
        script = probe + '''. "$1"
cix_run="$2/run"
cix_log="$2/log"
printf() {
  if [ "$FAIL_PID_WRITE" = yes ] && [ "$#" -eq 2 ]; then return 1; fi
  command printf "$@"
}
cix_power_start
'''
        result = execute([shell, '-c', script, 'power-contract', str(library), str(directory)],
                         dict({'POWER_DEVICE': '/dev/null'}, **(extra or {})))
        return result.returncode == 0, result.stdout, directory

    live = []
    try:
        ok, _, directory = run_start('success')
        pidfile = directory / 'run/cix-powerd/pid'
        if pidfile.is_file():
            live.append(int(pidfile.read_text()))
        check(ok, 'native foreground powerd launches')
        check(trace.read_text() == '-d\n', 'debug mode retains stderr without syslogd')
        check(pidfile.is_file() and live[-1] > 1, 'record launched child PID')
        check((directory / 'log/powerd.log').stat().st_mode & 0o777 == 0o600, 'private daemon log')
        ok, output, _ = run_start('success')
        check(not ok and not trace.read_text(), 'duplicate startup cannot launch daemon')
        check('powerd' in output, 'duplicate startup visible')
        ok, output, directory = run_start('failed-child', {'FAIL_POWERD': 'yes'})
        check(not ok and 'synthetic power open failure' in output, 'child failure reaches console')
        check('synthetic power open failure' in (directory / 'log/powerd.log').read_text(), 'private log retains child error')
        check(not (directory / 'run/cix-powerd').exists(), 'failed startup releases guard')
        ok, _, directory = run_start('failed-pid-write', {'FAIL_PID_WRITE': 'yes'})
        check(not ok and not (directory / 'run/cix-powerd').exists(), 'failed PID write releases guard')
        ok, _, directory = run_start('failed-pid-write')
        pidfile = directory / 'run/cix-powerd/pid'
        if pidfile.is_file():
            live.append(int(pidfile.read_text()))
        check(ok and pidfile.is_file(), 'failed PID write permits retry')
        ok, _, _ = run_start('missing-device', {'POWER_DEVICE': str(tmp / 'no-device')})
        check(not ok and not trace.read_text(), 'missing character device cannot launch powerd')
        (tmp / 'regular-device').write_text('not a character device')
        ok, _, _ = run_start('bad-regular-device', {'POWER_DEVICE': str(tmp / 'regular-device')})
        check(not ok and not trace.read_text(), 'regular device impostor cannot launch powerd')
        (tmp / 'bad-log/log/powerd.log').mkdir(parents=True)
        ok, _, directory = run_start('bad-log')
        check(not ok and not trace.read_text() and not (directory / 'run/cix-powerd').exists(), 'log-open failure cannot launch')
        (binary / 'powerd').rename(binary / 'powerd.saved')
        try:
            ok, output, directory = run_start('missing-program')
            check(not ok and 'powerd' in output and not (directory / 'run/cix-powerd').exists(), 'missing program releases guard')
        finally:
            (binary / 'powerd.saved').rename(binary / 'powerd')
        rcroot = tmp / 'rc-root'
        for directory in ('etc', 'var/run', 'var/db'):
            (rcroot / directory).mkdir(parents=True, exist_ok=True)
        (rcroot / 'etc/cix-root.subr').write_text('cix_root_start() { echo root >> "$TRACE"; }\n')
        (rcroot / 'etc/cix-network.subr').write_text('cix_network_start() { echo network >> "$TRACE"; }\n')
        shutil.copyfile(library, rcroot / 'etc/cix-power.subr')
        shutil.copyfile(here / 'cix-p1-ssh.subr', rcroot / 'etc/cix-ssh.subr')
        rc = Path(os.environ.get('RC_SCRIPT', here / 'cix-p1-rc')).read_text()
        rc = re.sub(r'/(etc|var)/', lambda m: str(rcroot) + '/' + m.group(1) + '/', rc)
        rc = re.sub(r'^PATH=.*$', 'PATH=' + shlex.quote(env['PATH']), rc, count=1, flags=re.MULTILINE)
        rcpath = tmp / 'rc'
        rcpath.write_text(rc)
        rcenv = dict(cix_run=f'{rcroot}/var/run', cix_log=f'{rcroot}/var/log',
                     POWER_DEVICE='/dev/null', cix_ssh_etc=f'{rcroot}/etc/ssh')
        trace.write_text('')
        result = execute([shell, '-c', probe + '. "$1"', 'rc-contract', str(rcpath)], rcenv)
        rcpid = rcroot / 'var/run/cix-powerd/pid'
        if rcpid.is_file():
            live.append(int(rcpid.read_text()))
        check(result.returncode == 0 and rcpid.is_file(), 'actual rc starts native power daemon')
        check(trace.read_text().splitlines() == ['root', '-d', 'network'], 'power follows root and precedes network')
        check('background' in result.stdout, 'console reports asynchronous network startup')
        shutil.rmtree(rcroot / 'var/run/cix-powerd')
        trace.write_text('')
        result = execute([shell, '-c', probe + '. "$1"', 'rc-contract', str(rcpath)], dict(rcenv, FAIL_POWERD='yes'))
        check(result.returncode == 0 and 'WARNING: powerd startup failed' in result.stdout, 'rc exposes failure and preserves console')
        check('network' in trace.read_text(), 'power failure does not suppress network')
        text = policy.read_text()
        check(text.count('/sbin/shutdown') == 1, 'one explicit shutdown boundary can be isolated')
        action, lock = tmp / 'power_button', tmp / 'poweroff.pending'
        action.write_text(text.replace('/sbin/shutdown', shlex.quote(str(binary / 'shutdown-double')), 1)
                          .replace('/var/run/cix-poweroff.pending', str(lock)))

        def invoke(event, extra=None):
            return execute([shell, str(action), 'acpibut0', event], extra)

        trace.write_text('')
        result = invoke('released')
        check(result.returncode == 0 and not trace.read_text() and not lock.exists(), 'release does not request shutdown')
        result = invoke('unknown')
        check(result.returncode != 0 and not trace.read_text(), 'unsupported event cannot request shutdown')
        result = invoke('pressed')
        check(result.returncode == 0 and trace.read_text() == 'shutdown <-D> <-i> <-p> <now> <power button pressed>\n', 'press requests orderly poweroff')
        check(lock.is_dir(), 'accepted request keeps duplicate guard')
        result = invoke('pressed')
        check(result.returncode == 0 and len(trace.read_text().splitlines()) == 1, 'duplicate press does not repeat shutdown')
        shutil.rmtree(lock)
        trace.write_text('')
        result = invoke('pressed', {'FAIL_SHUTDOWN': 'yes'})
        check(result.returncode != 0 and not lock.exists(), 'shutdown failure releases guard')
        result = invoke('pressed')
        check(result.returncode == 0 and len(trace.read_text().splitlines()) == 2, 'failed request permits retry')
        shutil.rmtree(lock)
        trace.write_text('')
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(invoke, ['pressed'] * 4))
        check(all(r.returncode == 0 for r in results) and len(trace.read_text().splitlines()) == 1,
              'concurrent presses admit exactly one shutdown request')
    finally:
        for pid in live:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

print(f'CIX power policy: {checks} checks, 0 failures')
