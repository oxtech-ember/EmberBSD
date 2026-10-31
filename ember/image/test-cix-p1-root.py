#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the CIX root startup contract without changing the host root."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).with_name('cix-p1-root.subr')
assert source.is_file(), 'missing root startup library'
checks = 0

with tempfile.TemporaryDirectory(prefix='cix-root-') as name:
    tmp = Path(name)
    shell = os.environ.get('TEST_SH')
    if not shell or not os.path.isabs(shell):
        raise SystemExit('Set TEST_SH to the selected native shell')
    for command in ('fsck', 'mount', 'mount_tmpfs', 'reboot'):
        path = tmp / command
        path.write_text(f'''#!{shell}
echo '{command}'" $*" >> "$TRACE"
[ '{command}' != fsck ] || exit "$FSCK_STATUS"
[ '{command}' != "$FAIL" ] || exit 9
exit 0
''')
        path.chmod(0o755)
    trace = tmp / 'trace'

    def run(status, failure=''):
        trace.write_text('')
        env = dict(os.environ, PATH=f'{tmp}:{os.environ["PATH"]}', TRACE=str(trace),
                   FSCK_STATUS=str(status), FAIL=failure)
        result = subprocess.run([shell, '-c',
                                 '. "$1"; cix_root_start', 'contract', str(source)],
                                env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        return result.returncode == 0, trace.read_text().splitlines(), result.stdout

    def check(condition, message):
        global checks
        checks += 1
        assert condition, message

    ok, lines, _ = run(0)
    check(ok, 'clean root starts')
    check(lines == ['fsck -p /', 'mount -u -w /',
                    'mount_tmpfs -s 32M tmpfs /var/run',
                    'mount_tmpfs -s 128M tmpfs /tmp'],
          'check root before writable mount and isolate runtime paths')
    for code in (2, 8, 12, 99):
        ok, lines, _ = run(code)
        check(not ok and lines == ['fsck -p /'],
              f'fsck {code} returns to single user without writes')
    ok, lines, _ = run(4)
    check(not ok and lines == ['fsck -p /', 'reboot -n'],
          'modified mounted root reboots without sync')
    ok, lines, _ = run(0, 'mount')
    check(not ok and len(lines) == 2, 'writable mount failure stops boot')
    ok, lines, _ = run(0, 'mount_tmpfs')
    check(not ok and len(lines) == 3, 'runtime tmpfs failure stops boot')

print(f'CIX root startup: {checks} checks, 0 failures')
