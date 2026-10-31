#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the native crunchgen dispatcher generated from the console-tools profile."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--crunchgen', required=True, type=Path)
p.add_argument('--host-prefix', required=True, type=Path)
a = p.parse_args()
source = Path(__file__).resolve().parents[2]
script = (source / 'ember/image/cix-p1-build-userland.sh').read_text()
start = script.index("        {\n            printf 'srcdirs bin sbin")
end = script.index('        } > "$out/crunch/console-tools.conf"', start)
block = script[start:end] + '        } > "$out/crunch/console-tools.conf"\n'
prefix = a.host_prefix.resolve(strict=True)
shell = prefix / 'bin/bash'
clang = prefix / 'bin/clang'
crunchgen = a.crunchgen.resolve(strict=True)
env = dict(os.environ, PATH=str(prefix / 'bin') + os.pathsep + str(prefix / 'gnu/bin'))
with tempfile.TemporaryDirectory() as td:
    out = Path(td)
    (out / 'crunch').mkdir()
    env.update(out=str(out), programs='bin/sh')
    subprocess.run([str(shell), '-c', 'set -eu\n' + block], env=env, check=True)
    wd = out / 'crunch'
    # This dispatcher-only test supplies an object name without invoking make.
    with (wd / 'console-tools.conf').open('a') as config:
        config.write('special sh objs sh.ro\n')
    subprocess.run([str(crunchgen), '-o', '-D', str(source), 'console-tools.conf'],
                   cwd=wd, env=env, check=True, stdout=subprocess.DEVNULL)
    (wd / 'stub.c').write_text('#include <stdio.h>\n'
        'int _crunched_sh_stub(int argc,char **argv,char **envp) '
        '{ (void)argc; (void)envp; puts(argv[0]); return 0; }\n')
    exe = wd / 'dispatch'
    subprocess.run([str(clang), '-D__RCSID(x)=', str(wd / 'console-tools.c'),
                    str(wd / 'stub.c'), '-o', str(exe)], env=env, check=True)
    for argv0 in ('sh', '-sh'):
        r = subprocess.run([argv0], executable=str(exe), env=env,
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if r.returncode != 0 or r.stdout != argv0 + '\n':
            raise SystemExit(f'dispatch failed for {argv0}: exit {r.returncode}, {r.stderr}')
print('native crunchgen dispatch preserves sh and -sh argv[0]')
