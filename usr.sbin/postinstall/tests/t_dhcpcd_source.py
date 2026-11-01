#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise postinstall's actual dhcpcd source selection with stubbed I/O."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--host-prefix', required=True, type=Path)
args = parser.parse_args()
prefix = args.host_prefix.resolve(strict=True)
shell = (prefix / 'bin/mksh').resolve(strict=True)
if not shell.is_relative_to(prefix):
    parser.error('mksh must resolve inside the selected host prefix')
text = (args.source / 'usr.sbin/postinstall/postinstall.in').read_text()

def function(name):
    start = text.index('\n' + name + '()\n{') + 1
    return text[start:text.index('\n}\n', start) + 3]

program = '''
msg() { :; }
err() { exit "$1"; }
populate_dir() { printf 'selected=%s\\n' "$3"; }
check_dir() { :; }
move_file() { :; }
find() { :; }
sort() { :; }
obsolete_paths() { :; }
contents_owner() { :; }
''' + function('find_file_in_dirlist') + function('do_dhcpcd') + '\ndo_dhcpcd check\n'
with tempfile.TemporaryDirectory(prefix='postinstall-dhcpcd-') as work:
    root = Path(work)
    (root / 'os/etc').mkdir(parents=True)
    (root / 'ports/src').mkdir(parents=True)
    (root / 'dest').mkdir()
    (root / 'os/etc/dhcpcd.conf').write_text('base\n')
    (root / 'ports/src/dhcpcd.conf').write_text('common\n')
    env = {'PATH': str(prefix / 'bin') + os.pathsep + str(prefix / 'gnu/bin'),
           'SRC_DIR': str(root / 'os'), 'DEST_DIR': str(root / 'dest'),
           'EMBER_DHCPCD_SOURCE': str(root / 'ports')}
    def run():
        return subprocess.run([str(shell), '-c', program], env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    result = run()
    if result.returncode or 'selected=' + str(root / 'ports/src') not in result.stdout:
        raise SystemExit('prepared source selection failed: ' + result.stderr)
    del env['EMBER_DHCPCD_SOURCE']
    result = run()
    if result.returncode or 'selected=' + str(root / 'os/etc') not in result.stdout:
        raise SystemExit('release etc fallback failed: ' + result.stderr)
    (root / 'os/etc/dhcpcd.conf').unlink()
    if run().returncode == 0:
        raise SystemExit('missing configuration accepted')
print('PASS: postinstall prepared dhcpcd source and release etc fallback')
