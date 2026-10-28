#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise ordinary grep behavior of a caller-selected runnable binary."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--grep', type=Path, required=True)
a = p.parse_args()
grep = str(a.grep.resolve(strict=True))
env = dict(os.environ, LC_ALL='C')
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    root = Path(temporary)
    (root / 'first').write_text('alpha\nBeta\na+b\n')
    (root / 'sub').mkdir()
    (root / 'sub' / 'second').write_text('beta\ngamma\n')
    def check(args, expected, code=0, data=None):
        result = subprocess.run([grep, *args], input=data, cwd=root, env=env,
                                capture_output=True)
        assert result.returncode == code, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, result.stdout)
    check(['-n', '^a', 'first'], b'1:alpha\n3:a+b\n')
    check(['-E', '^alpha|gamma$', 'first'], b'alpha\n')
    check(['-F', 'a+b', 'first'], b'a+b\n')
    check(['-i', '-w', 'beta', 'first'], b'Beta\n')
    check(['-r', '-l', 'gamma', '.'], b'./sub/second\n')
    check(['-z', 'two'], b'two\0', data=b'one\0two\0')
    check(['missing', 'first'], b'', code=1)
    check(['-P', 'a', 'first'], b'', code=2)
print('ordinary GNU grep behavior: PASS')
