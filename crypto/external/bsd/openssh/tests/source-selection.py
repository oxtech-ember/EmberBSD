#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the common OpenSSH selector without building any binaries."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--work-dir', type=Path, required=True)
args = parser.parse_args()
selector = Path(__file__).resolve().parents[1] / 'source.mk'
env = os.environ.copy()
for name in ('EMBER_OPENSSH_SOURCE', 'SSHDIST', 'HOST_AUTOCONF'):
    env.pop(name, None)
with tempfile.TemporaryDirectory(dir=args.work_dir) as work:
    makefile = Path(work) / 'Makefile'
    makefile.write_text('NETBSDSRCDIR=/example/os\n.include "' + str(selector) + '"\nall obj clean cleandir includes install:\n\t@:\n')
    base = [str(args.bmake.resolve(strict=True)), '-f', str(makefile)]
    def run(*words):
        return subprocess.run(base + list(words), env=env, capture_output=True, text=True)
    assert run('obj').returncode == 0
    assert run('clean').returncode == 0
    assert run('clean', 'cleandir').returncode == 0
    missing = run('all')
    assert missing.returncode and 'Set EMBER_OPENSSH_SOURCE' in missing.stderr
    source = str(args.source.resolve(strict=True))
    selected = run('EMBER_OPENSSH_SOURCE=' + source, '-V', '${SSHDIST}', 'all')
    assert selected.returncode == 0 and selected.stdout.strip() == source
    conflict = run('EMBER_OPENSSH_SOURCE=' + source, 'SSHDIST=/unrelated', 'all')
    assert conflict.returncode and 'must match' in conflict.stderr
    config = selector.with_name('config.mk')
    makefile.write_text('NETBSDSRCDIR=/example/os\n_SRC_TOP_OBJ_=/example/obj\n'
                        '.include "' + str(config) + '"\nall obj clean cleandir includes install:\n\t@:\n')
    selected_arg = 'EMBER_OPENSSH_SOURCE=' + source
    assert run(selected_arg, 'obj').returncode == 0
    assert run(selected_arg, 'clean').returncode == 0
    assert run(selected_arg, 'clean', 'cleandir').returncode == 0
    assert run(selected_arg, 'includes').returncode == 0
    assert run(selected_arg, 'install').returncode == 0
    assert run(selected_arg, '-V', 'OPENSSH_CONFIG_HEADER').returncode == 0
    missing_tool = run(selected_arg, 'all')
    assert missing_tool.returncode and 'requires selected HOST_AUTOCONF' in missing_tool.stderr
    assert run(selected_arg, 'HOST_AUTOCONF=/example/autoconf', 'all').returncode == 0
print('PASS: common OpenSSH source selection')
