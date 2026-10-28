#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check host-tool routing and Info metadata without the legacy Texinfo tree."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--tool-cat', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
prefix = a.prefix.resolve(strict=True)
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    tools = Path(temporary) / 'tools'
    (tools / 'bin').mkdir(parents=True)
    (tools / 'bin/nbcat').symlink_to(a.tool_cat.resolve(strict=True))
    env = dict(os.environ, PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'))
    common = [str(prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
              'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
              'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
              'TOOLDIR=' + str(tools), 'HOST_SH=' + str(prefix / 'bin/mksh'),
              'HOST_YACC=' + str(prefix / 'bin/byacc')]
    def run(directory, *arguments):
        return subprocess.run([*common, '-C', str(root / directory), *arguments],
                              env=env, capture_output=True, text=True)
    for extra in ((), ('EMBER_NATIVE_TOOLS=/selected/native-tools',)):
        result = run('tools', *extra, '-V', '${SUBDIR}')
        assert result.returncode == 0, result.stderr
        assert 'texinfo' not in result.stdout.split(), result.stdout
    for target in ('all', 'dependall', 'install'):
        result = run('tools', '-n', target)
        assert result.returncode != 0 and 'EMBER_NATIVE_TOOLS' in result.stderr, result.stderr
    for target in ('obj', 'includes', 'clean', 'cleandir'):
        result = run('tools', '-n', target)
        assert 'Set EMBER_NATIVE_TOOLS' not in result.stderr, result.stderr
    result = run('external/gpl2/texinfo/bin', 'MKUNPRIVED=yes', 'MKINFO=yes',
                 'METALOG=' + str(Path(temporary) / 'METALOG'), 'infodir-meta')
    assert result.returncode == 0, result.stderr
    text = (Path(temporary) / 'METALOG').read_text()
    assert './usr/share/info/dir type=file mode=0644' in text, text
    for choice in (('MKUNPRIVED=no', 'MKINFO=yes'), ('MKUNPRIVED=yes', 'MKINFO=no')):
        target = Path(temporary) / ('disabled-' + choice[0] + choice[1])
        result = run('external/gpl2/texinfo/bin', *choice, 'METALOG=' + str(target), 'infodir-meta')
        assert result.returncode == 0 and not target.exists(), result.stderr
print('Texinfo host routing and Info metadata: PASS')
