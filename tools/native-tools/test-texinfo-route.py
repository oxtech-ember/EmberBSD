#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check OS Texinfo routing and exercise the selected host programs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', required=True, type=Path)
args = parser.parse_args()
prefix = args.prefix.resolve(strict=True)
make = prefix / 'bin/bmake'
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    work = Path(temporary)
    route = work / 'route'
    route.mkdir()
    for name in ('makeinfo', 'install-info', 'perl'):
        (route / name).symlink_to(prefix / 'bin' / name)
    common = [str(make), '-r', '-m', str(root / 'share/mk'), '-C', str(root / 'tools'),
              'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
              'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
              'TOOLDIR=' + str(work / 'tools')]
    selected = subprocess.run([*common, 'EMBER_NATIVE_TOOLS=' + str(route),
                               '-V', '${TOOL_MAKEINFO}', '-V', '${TOOL_INSTALL_INFO}',
                               '-V', '${SUBDIR:Mtexinfo}'], check=True, text=True,
                              capture_output=True).stdout.splitlines()
    assert selected == [str(route / 'makeinfo'), str(route / 'install-info'), ''], selected
    legacy = subprocess.run([*common, '-V', '${SUBDIR:Mtexinfo}'], check=True,
                            text=True, capture_output=True).stdout.strip()
    assert legacy == 'texinfo', legacy
    env = dict(os.environ, PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'))
    source = work / 'sample.texi'
    source.write_text('\\input texinfo\n@setfilename sample.info\n@dircategory Test\n@direntry\n* Sample: (sample). Test entry.\n@end direntry\n@node Top\n@top Sample\nSample text.\n@bye\n')
    info = work / 'sample.info'
    makefile = work / 'Makefile'
    makefile.write_text('TEXINFO= sample.texi\n.include <bsd.info.mk>\n')
    subprocess.run([str(make), '-r', '-m', str(root / 'share/mk'), '-C', str(work),
                    'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
                    'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
                    'TOOLDIR=' + str(work / 'tools'), 'EMBER_NATIVE_TOOLS=' + str(route),
                    'sample.info'], env=env, check=True)
    assert ', produced from ' in info.read_text()
    assert ', produced by ' not in info.read_text()
    directory = work / 'dir'
    subprocess.run([selected[1], '-d', str(directory), str(info)], env=env, check=True)
    assert info.is_file() and '* Sample: (sample).' in directory.read_text()
    subprocess.run([selected[1], '-d', str(directory), '-r', str(info)], env=env, check=True)
    assert '* Sample: (sample).' not in directory.read_text()
    subprocess.run([selected[1], '-d', str(directory), str(info)], env=env, check=True)
    assert '* Sample: (sample).' in directory.read_text()
print('native Texinfo selection and execution: PASS')
