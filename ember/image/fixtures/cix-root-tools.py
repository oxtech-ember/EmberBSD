#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Synthetic image tools: no media operations are ever permitted."""
import hashlib
import json
import os
from pathlib import Path
import sys

name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['CALL_LOG'], 'a') as log:
    log.write(json.dumps([name, *args]) + '\n')
if name == 'shasum':
    if args == ['-a', '256', os.environ['FIXTURE_FIRMWARE']]:
        data = Path(args[-1]).read_bytes()
        value = (os.environ['FIXTURE_FW_HASH'] if data == b'synthetic-approved-firmware\n'
                 else hashlib.sha256(data).hexdigest())
        print(f'{value}  {args[-1]}')
    else:
        os.execv(os.environ['REAL_SHASUM'], [os.environ['REAL_SHASUM'], *args])
elif name == 'nbpwd_mkdb':
    if args[:3] != ['-L', '-p', '-d'] or len(args) != 5:
        raise SystemExit('unexpected password-db arguments')
    root = Path(args[3])
    if args[4] != str(root / 'etc/master.passwd'):
        raise SystemExit('unexpected password source')
    (root / 'etc/pwd.db').write_text('synthetic-public-db')
    (root / 'etc/spwd.db').write_text('synthetic-protected-db')
elif name == 'nbmtree':
    if args != ['-c', '-p', os.environ['FIXTURE_OUT'] + '/root', '-k', 'type,mode,link']:
        raise SystemExit('unexpected mtree arguments')
    print('/set type=file mode=0755\n. type=dir mode=0755\n./etc type=dir mode=0755\n./etc/master.passwd type=file mode=0600')
elif name == 'nbmakefs':
    if any(a.startswith('/dev/') for a in args):
        raise SystemExit('device argument passed to formatter')
    out = os.environ['FIXTURE_OUT']
    expected = ['-t', 'ffs', '-B', 'little', '-s', '2g', '-o', 'version=2,bsize=16384,fsize=2048',
                '-F', out + '/root.spec', '-N', os.environ['FIXTURE_SOURCE'] + '/etc', out + '/root.fs', out + '/root']
    if args != expected:
        raise SystemExit('unexpected image specification or destination')
    Path(args[-2]).write_text('synthetic-ffs-image')
    if os.environ.get('MUTATE_TRACKED_ASSET'):
        with open(os.environ['MUTATE_TRACKED_ASSET'], 'a') as output:
            output.write('# changed during image assembly\n')
    if os.environ.get('EXTRA_STAGED_DATA'):
        Path(out + '/root/usr/share/i18n/unrecorded').write_text('unexpected DATA')
    if os.environ.get('MUTATE_STAGED_DATA'):
        path = Path(out + '/root') / os.environ['MUTATE_STAGED_DATA']
        path.chmod(0o644)
        with path.open('a') as output:
            output.write('changed staged data')
        path.chmod(0o444)
else:
    raise SystemExit('forbidden media action: ' + name)
