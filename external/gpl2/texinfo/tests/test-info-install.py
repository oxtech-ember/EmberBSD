#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Install and re-register both Texinfo manuals through the OS rules."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('prefix', 'source', 'tools', 'sortinfo', 'native-tools'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
    work = Path(temp)
    obj = work / 'obj'
    dest = work / 'dest'
    obj.mkdir()
    (dest / 'usr/share/info').mkdir(parents=True)
    env = dict(os.environ, MAKEOBJDIR=str(obj), PATH=str(a.prefix / 'bin') + ':' + str(a.prefix / 'gnu/bin'))
    make = [str(a.prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'), '-C',
            str(root / 'external/gpl2/texinfo'), 'MAKECONF=/dev/null', '_SRC_TOP_=',
            'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
            'USETOOLS=yes', 'TOOLDIR=' + str(a.tools), 'EMBER_TEXINFO_SOURCE=' + str(a.source),
            'EMBER_NATIVE_TOOLS=' + str(a.native_tools), 'TOOL_MAKEINFO=' + str(a.prefix / 'bin/makeinfo'),
            'TOOL_INSTALL_INFO=' + str(a.prefix / 'bin/install-info'), 'TOOL_SORTINFO=' + str(a.sortinfo),
            'MV=' + str(a.prefix / 'gnu/bin/mv'), 'INSTALL=' + str(a.tools / 'bin/aarch64--netbsd-install'),
            'DESTDIR=' + str(dest), 'INSTPRIV=-U -M ' + str(work / 'METALOG') + ' -D ' + str(dest) + ' -h sha256',
            'MKNLS=no', 'MKINFO=yes']
    subprocess.run([*make, 'info-stnd.info', 'texinfo.info'], env=env, check=True, capture_output=True)
    for _ in range(2):
        subprocess.run([*make, 'infoinstall'], env=env, check=True, capture_output=True)
    info = dest / 'usr/share/info'
    directory = (info / 'dir').read_text()
    assert directory.count('* Texinfo:') == 1 and directory.count('* info stand-alone:') == 1
    assert (info / 'info-stnd.info').is_file() and (info / 'texinfo.info').is_file()
    assert not (info / 'dir.lock').exists()
    failed = subprocess.run([*make, 'TOOL_SORTINFO=' + str(a.prefix / 'gnu/bin/false'), 'infoinstall'],
                            env=env, capture_output=True)
    assert failed.returncode != 0 and not (info / 'dir.lock').exists()
print('Texinfo Info manual registration and failure propagation: PASS')
