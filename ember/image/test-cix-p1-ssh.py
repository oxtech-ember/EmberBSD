#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise opt-in SSH startup against controlled command boundaries."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

source = Path(__file__).with_name('cix-p1-ssh.subr')
shell = os.environ.get('TEST_SH')
if not shell or not os.path.isabs(shell):
    raise SystemExit('Set TEST_SH to the selected native shell')
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


with tempfile.TemporaryDirectory(prefix='cix-ssh-') as name:
    tmp = Path(name)
    for path in ('bin', 'sbin', 'libexec', 'etc/ssh', 'run', 'pts'):
        (tmp / path).mkdir(parents=True, exist_ok=True)
    trace = tmp / 'trace'
    config = tmp / 'etc/ssh/sshd_config'
    config.write_text('# synthetic administrator configuration, no secrets\n')
    config.chmod(0o600)
    scripts = {
        'bin/stat': 'case "$3" in */sshd_config) echo "$CONFIG_MODE";; *) echo "$DIRECTORY_MODE";; esac\n',
        'sbin/sshd': 'printf \'sshd %s\\n\' "$*" >> "$TRACE"\ncase "$1" in -t) [ "$FAIL" != config ];; *) [ "$FAIL" != daemon ];; esac\n',
        'bin/mount': '[ "$#" = 0 ] || { echo "native mount rejects options" >&2; exit 91; }\nprintf \'mount\\n\' >> "$TRACE"\n[ "$FAIL" != mount-list ] || exit 9\nprintf \'%s\\n\' "$MOUNTS"\n',
        'bin/mount_ptyfs': 'printf \'mount_ptyfs %s\\n\' "$*" >> "$TRACE"\n[ "$FAIL" != mount ]\n',
        'libexec/sshd-session': 'exit 91\n', 'libexec/sshd-auth': 'exit 91\n',
    }
    for path, body in scripts.items():
        (tmp / path).write_text(f'#!{shell}\n' + body)
        (tmp / path).chmod(0o755)

    def run(failure='', mode='0:600', mounts='', directory='0:755', twice=False):
        trace.write_text('')
        before = config.read_bytes() if config.is_file() else None
        env = dict(os.environ, PATH=f'{tmp}/bin:{os.environ["PATH"]}', TRACE=str(trace),
                   FAIL=failure, CONFIG_MODE=mode, DIRECTORY_MODE=directory, MOUNTS=mounts)
        body = '''. "$1" || exit 99
cix_ssh_etc="$2/etc/ssh"
cix_ssh_sbin="$2/sbin"
cix_ssh_libexec="$2/libexec"
cix_ssh_run="$2/run"
cix_ssh_pts="$2/pts"
cix_ssh_start || exit $?
''' + ('cix_ssh_start' if twice else ':')
        result = subprocess.run([shell, '-c', body, 'contract', str(source), str(tmp)], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        check(before == (config.read_bytes() if config.is_file() else None), 'administrator config is unchanged')
        return result.returncode == 0, trace.read_text().splitlines(keepends=True), result.stdout

    ok, events, output = run()
    check(ok, f'valid opt-in starts SSH: {output} {events}')
    check(events == [f'sshd -t -f {config}\n', 'mount\n', f'mount_ptyfs ptyfs {tmp}/pts\n',
                     f'sshd -f {config}\n'], 'validate before PTY mount and daemon launch')
    saved = config.with_name('sshd_config.saved')
    config.rename(saved)
    ok, events, _ = run()
    check(ok and not events, 'absent config disables SSH without mount or daemon effects')
    check(not config.exists(), 'disabled startup creates no config')
    config.symlink_to(saved)
    ok, events, _ = run()
    check(not ok and not events, 'symlink config cannot enable SSH')
    config.unlink()
    config.symlink_to(config.with_name('sshd_config.missing'))
    ok, events, _ = run()
    check(not ok and not events, 'dangling config symlink is rejected')
    config.unlink()
    saved.rename(config)
    for mode in ('1000:600', '0:644', '0:400', '0:1600'):
        ok, events, _ = run(mode=mode)
        check(not ok and not events, f'unsafe config metadata is rejected: {mode}')
    for mode in ('1000:755', '0:777'):
        ok, events, _ = run(directory=mode)
        check(not ok and not events, f'unsafe SSH directory is rejected: {mode}')
    for helper in ('sshd-session', 'sshd-auth'):
        path = tmp / 'libexec' / helper
        path.chmod(0o644)
        ok, events, _ = run()
        check(not ok and not events, f'missing executable helper: {helper}')
        path.chmod(0o755)
    pid = tmp / 'run/sshd.pid'
    pid.write_text('synthetic existing instance')
    ok, events, _ = run()
    check(not ok and not events, 'existing PID file rejects duplicate daemon')
    pid.unlink()
    for failure, count, message in (('config', 1, 'bad config'), ('mount-list', 2, 'mount inventory'),
                                     ('mount', 3, 'PTY mount'), ('daemon', 4, 'daemon failure')):
        ok, events, _ = run(failure)
        check(not ok and len(events) == count, message)
    ok, events, _ = run(mounts=f'ptyfs on {tmp}/pts type ptyfs (local)')
    check(ok and not any(e.startswith('mount_ptyfs') for e in events), 'existing ptyfs is not mounted twice')
    ok, events, _ = run(mounts=f'tmpfs on {tmp}/pts type tmpfs (local)')
    check(not ok and len(events) == 2, 'conflicting mount is not covered or used')
    ok, events, _ = run(mounts=f'/dev/dk1 on / type ffs (local)\ntmpfs on /var/run type tmpfs (local)\nptyfs on {tmp}/pts type ptyfs')
    check(ok and not any(e.startswith('mount_ptyfs') for e in events), 'mount output with or without flags')
    ok, events, _ = run(mounts=f'ptyfs on {tmp}/pts.other type ptyfs (local)')
    check(ok and any(e.startswith('mount_ptyfs') for e in events), 'mountpoint prefix cannot hide missing exact mount')
    for listing in (f'ptyfs on {tmp}/pts type ptyfs\nptyfs on {tmp}/pts type ptyfs',
                    f'ptyfs {tmp}/pts ptyfs rw 0 0', f'ptyfs on {tmp}/pts nonsense ptyfs'):
        ok, events, _ = run(mounts=listing)
        check(not ok and len(events) == 2, 'duplicate or unrecognized mount listing')
    ok, events, _ = run(twice=True)
    check(not ok and len(events) == 4, 'same startup context cannot launch twice')

    (tmp / 'etc/cix-root.subr').write_text('cix_root_start() { echo root >> "$TRACE"; [ "$ROOT_FAIL" != yes ]; }\n')
    (tmp / 'etc/cix-network.subr').write_text('cix_network_start() { echo network >> "$TRACE"; return 9; }\n')
    (tmp / 'etc/cix-power.subr').write_text('cix_power_start() { echo power >> "$TRACE"; return 0; }\n')
    shutil.copyfile(source, tmp / 'etc/cix-ssh.subr')
    for command in ('mkdir', 'chmod', 'hostname', 'ifconfig', 'rndctl'):
        path = tmp / 'bin' / command
        path.write_text(f'#!{shell}\nprintf \'%s %s\\n\' \'{command}\' "$*" >> "$TRACE"\n')
        path.chmod(0o755)
    rc_source = Path(os.environ.get('CIX_RC_SOURCE', Path(__file__).with_name('cix-p1-rc')))
    rc = re.sub(r'/(?:etc|var)/', lambda m: str(tmp) + m.group(), rc_source.read_text())
    fake_path = f'{tmp}/bin:{os.environ["PATH"]}'
    rc = rc.replace('PATH=/sbin:/bin:/usr/sbin:/usr/bin', f'PATH={fake_path}', 1)
    assert f'PATH={fake_path}\n' in rc, 'unsafe rc fixture PATH'
    (tmp / 'rc').write_text(rc)

    def rc_run(root_failure='', ssh_failure=''):
        trace.write_text('')
        env = dict(os.environ, TRACE=str(trace), ROOT_FAIL=root_failure, FAIL=ssh_failure,
                   CONFIG_MODE='0:600', DIRECTORY_MODE='0:755', MOUNTS='',
                   cix_ssh_etc=f'{tmp}/etc/ssh', cix_ssh_sbin=f'{tmp}/sbin',
                   cix_ssh_libexec=f'{tmp}/libexec', cix_ssh_run=f'{tmp}/run', cix_ssh_pts=f'{tmp}/pts')
        result = subprocess.run([shell, str(tmp / 'rc')], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        return result.returncode == 0, trace.read_text().splitlines(keepends=True), result.stdout

    ok, events, output = rc_run()
    ssh = events.index(f'sshd -f {config}\n') if f'sshd -f {config}\n' in events else None
    entropy = next((i for i, e in enumerate(events) if e.startswith('rndctl -S ')), None)
    network = events.index('network\n') if 'network\n' in events else None
    check(ok and ssh is not None and entropy is not None and network is not None and entropy < ssh < network,
          f'actual rc orders entropy, SSH and network: {events!r} {output!r}')
    check('WARNING: Wi-Fi startup failed.' in output, 'network failure remains visible')
    ok, events, _ = rc_run('yes')
    check(not ok and events == ['root\n'], 'failed root check cannot start SSH or networking')
    ok, events, output = rc_run('', 'daemon')
    check(ok and 'network\n' in events and 'WARNING: SSH startup failed.' in output,
          'SSH failure warns without preventing local-console/network startup')
    check([p.name for p in (tmp / 'etc/ssh').iterdir()] == ['sshd_config'], 'no host keys or default config generated')

print(f'CIX SSH startup: {checks} checks, 0 failures')
