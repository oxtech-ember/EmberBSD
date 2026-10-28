#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the actual Texinfo GNU target producer's source selection."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--host-triple', required=True)
args = p.parse_args()
prefix = args.prefix.resolve(strict=True)
source = args.source.resolve(strict=True)
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    toolroot = Path(temporary) / 'tools'
    base = [str(prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'), '-C',
            str(root / 'external/gpl2/texinfo/bin'), 'MAKECONF=/dev/null', '_SRC_TOP_=',
            'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
            'USETOOLS=yes', 'TOOLDIR=' + str(toolroot), 'HOST_SH=' + str(prefix / 'bin/mksh'),
            'HOST_GMAKE=' + str(prefix / 'bin/gmake'), 'EMBER_HOST_GNU_TRIPLE=' + args.host_triple]
    env = dict(os.environ, PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'))
    result = subprocess.run([*base, 'EMBER_TEXINFO_SOURCE=' + str(source),
                            '-V', '${GNU_SOURCE}', '-V', '${GNU_CONFIGURE_ARGS}',
                            '-V', '${GNU_MAKE_DIRS}', '-V', '${PROGS}',
                            '-V', '${GNU_HOST_MAKE_DIRS}', '-V', '${GNU_HOST_MAKE_TARGETS.tools/info}'],
                            env=env, capture_output=True, text=True, check=True)
    actual = result.stdout.splitlines()
    assert actual[0] == str(source), actual
    assert '--disable-perl-tools' in actual[1].split(), actual
    assert '--enable-native-c-only-tools' in actual[1].split(), actual
    assert actual[2].split() == ['gnulib/lib', 'install-info', 'info'], actual
    assert actual[3].split() == ['info', 'install-info'], actual
    assert actual[4].split() == ['tools/gnulib/lib', 'tools/info'] and actual[5] == 'makedoc', actual
    for choice in ('yes', 'no'):
        result = subprocess.run([*base, 'EMBER_TEXINFO_SOURCE=' + str(source), 'MKNLS=' + choice,
                                 '-V', '${GNU_CONFIGURE_ARGS}'], env=env, capture_output=True, text=True, check=True)
        flags = result.stdout.split()
        assert ('--with-base-libintl' in flags) == (choice == 'yes'), flags
        assert ('--disable-nls' in flags) == (choice == 'no'), flags
    invalid = subprocess.run([*base, 'EMBER_TEXINFO_SOURCE=' + str(Path(temporary) / 'missing'),
                              '-V', '${GNU_SOURCE}'], env=env, capture_output=True, text=True)
    assert invalid.returncode != 0 and 'must name a prepared common Texinfo source' in invalid.stderr
    for directory in ('texinfo', 'texinfo/bin'):
        command = base.copy()
        command[command.index(str(root / 'external/gpl2/texinfo/bin'))] = str(root / 'external/gpl2' / directory)
        for target in ('obj', 'includes'):
            result = subprocess.run([*command, '-n', target], env=env, capture_output=True, text=True)
            assert result.returncode == 0, (directory, target, result.stderr)
        for target in ('all', 'dependall', 'install'):
            result = subprocess.run([*command, '-n', target], env=env, capture_output=True, text=True)
            assert result.returncode != 0 and 'Set EMBER_TEXINFO_SOURCE' in result.stderr
print('Texinfo target source routing: PASS')
