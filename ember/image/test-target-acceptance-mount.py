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
query = rc[rc.index('root_unit='):rc.index('EMBERBSD_ACCEPTANCE_NONCE=')]
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    mock = path / 'mount'
    mock.write_text('#!' + args.sh + '\nprintf "%s\\n" "$MOUNT_DATA"\nexit "$MOUNT_STATUS"\n')
    mock.chmod(0o755)
    sysctl = path / 'sysctl'
    sysctl.write_text('#!' + args.sh + '\ncase $2 in kern.root_device) printf "%s\\n" "$UNIT"; exit "$UNIT_STATUS";; kern.root_partition) printf "%s\\n" "$PART"; exit "$PART_STATUS";; esac\nexit 1\n')
    sysctl.chmod(0o755)
    query = query.replace('/sbin/mount', str(mock)).replace('/sbin/sysctl', str(sysctl)).replace('LC_ALL=C awk', 'LC_ALL=C ' + args.awk)
    script = 'fail() { exit 1; }\nroot_device=ld4a\nnonce=fixture\n' + query
    def check(data, status, expected, unit='ld4', part='0', unit_status=0, part_status=0):
        env = dict(os.environ, MOUNT_DATA=data, MOUNT_STATUS=str(status), UNIT=unit, PART=part,
                   UNIT_STATUS=str(unit_status), PART_STATUS=str(part_status))
        result = subprocess.run([args.sh, '-c', script], env=env, text=True, capture_output=True)
        assert (result.returncode == 0) == (expected is not None), (data, result.stderr)
        if expected is not None:
            assert result.stdout.strip() == expected
    valid = '/dev/ld4a on / type ffs (local, read-only)'
    marker = 'EMBERBSD-ACCEPTANCE fixture ROOT /dev/ld4a ffs ro'
    check(valid, 0, marker)
    check('/dev/ld4a on / type ffs (read-only, local)', 0, marker)
    check('root_device on / type ffs (read-only, local)', 0, marker)
    check('/dev/ld4a on / type ffs (local)', 0, None)
    check('/dev/ld4a on / type tmpfs (read-only)', 0, None)
    check('/dev/wd0a on / type ffs (read-only)', 0, None)
    check(valid + '\n' + valid, 0, None)
    check(valid, 1, None)
    check(valid, 0, None, unit='ld5')
    check(valid, 0, None, part='1')
    check(valid, 0, None, unit_status=1)
    check(valid, 0, None, part_status=1)
    check('/dev/ld5a on / type ffs (read-only)', 0, None)
    check('/dev/ld4a on / type ffs (not-read-only)', 0, None)
print('PASS: native mount query, readonly and failure handling')
