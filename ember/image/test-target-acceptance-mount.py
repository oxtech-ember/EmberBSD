#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the actual rc mount query using a selected shell and awk.
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--sh', required=True)
p.add_argument('--awk', required=True)
args = p.parse_args()
for tool in (args.sh, args.awk):
    if not os.path.isabs(tool) or not os.access(tool, os.X_OK):
        p.error('tools must be absolute selected executables')
rc = Path(__file__).with_name('target-acceptance-rc').read_text()
query = rc[rc.index('mount_output='):rc.index('set -- $mounted')]
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    mock = path / 'mount'
    mock.write_text('#!' + args.sh + '\nprintf "%s\\n" "$MOUNT_DATA"\nexit "$MOUNT_STATUS"\n')
    mock.chmod(0o755)
    query = query.replace('/sbin/mount', str(mock)).replace('LC_ALL=C awk', 'LC_ALL=C ' + args.awk)
    script = 'fail() { exit 1; }\n' + query + 'printf "%s\\n" "$mounted"\n'
    def check(data, status, expected):
        env = dict(os.environ, MOUNT_DATA=data, MOUNT_STATUS=str(status))
        result = subprocess.run([args.sh, '-c', script], env=env, text=True, capture_output=True)
        assert (result.returncode == 0) == (expected is not None), (data, result.stderr)
        if expected is not None:
            assert result.stdout.strip() == expected
    valid = '/dev/ld4a on / type ffs (local, read-only)'
    check(valid, 0, '/dev/ld4a ffs ro')
    check('/dev/ld4a on / type ffs (read-only, local)', 0, '/dev/ld4a ffs ro')
    check('/dev/ld4a on / type ffs (local)', 0, None)
    check('/dev/ld4a on / type tmpfs (read-only)', 0, None)
    check('/dev/wd0a on / type ffs (read-only)', 0, None)
    check(valid + '\n' + valid, 0, None)
    check(valid, 1, None)
    check('/dev/ld4a on / type ffs (not-read-only)', 0, None)
print('PASS: native mount query, readonly and failure handling')
