#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Record the source files that each portable kernel contract reads.

usage: kernel-contracts-trace.py SOURCE_ROOT

Set the contract environment first (CC, CXX, RUBY, NETBSD2_PYTHON), as for
kernel-contracts.sh. The file system must update access times on read.
The script runs each portable contract alone, finds the source files with a
new access time and adds them to kernel-contracts-inputs.txt. It does not
remove lines, so inputs that you add by hand stay.
"""
import os
import re
import subprocess
import sys
import time

INPUTS = 'ember/tools/kernel-contracts-inputs.txt'
OLD = 946684800  # 2000-01-01: older than each source modification time


def files(src):
    for base, dirs, names in os.walk(src):
        dirs[:] = [d for d in dirs if d != '.git']
        for name in names:
            yield os.path.join(base, name)


def age(paths):
    for path in paths:
        try:
            os.utime(path, (OLD, os.stat(path).st_mtime))
        except OSError:
            pass


def read_since(src, start):
    found = []
    for path in files(src):
        try:
            if os.stat(path).st_atime >= start:
                found.append(path)
        except OSError:
            pass
    return found


def entry(src, script, path):
    rel = os.path.relpath(path, src)
    if rel in (script, 'ember/tools/kernel-contracts.sh'):
        return None
    if rel.startswith('ember/') or '/' not in rel:
        return rel
    return os.path.dirname(rel) + '/'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[2])
    src = os.path.realpath(sys.argv[1])
    python = os.environ.get('NETBSD2_PYTHON') or sys.executable
    commands = []
    with open(os.path.join(src, 'ember/tools/kernel-contracts.sh'), encoding='utf-8') as stream:
        for line in stream:
            # Indented commands are native or need firmware. They keep their lines.
            match = re.match(r'run (.*"\$src/(ember/[^"]+)".*)$', line.rstrip('\n'))
            if match:
                commands.append((match.group(2), match.group(1)))
    age(files(src))
    table = set()
    with open(os.path.join(src, INPUTS), encoding='utf-8') as stream:
        lines = stream.read().splitlines()
    header = [line for line in lines if line.startswith('#')]
    table.update(line for line in lines if line and not line.startswith('#'))
    failed = 0
    for script, command in commands:
        start = time.time()
        time.sleep(1)
        result = subprocess.run(['sh', '-c', f'src=$1; python=$2; {command}', 'trace', src, python],
                                cwd=src, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode != 0:
            print(f'contract failed, not recorded: {script}', file=sys.stderr)
            failed += 1
        read = read_since(src, start)
        age(read)
        if result.returncode == 0:
            inputs = {entry(src, script, path) for path in read} - {None}
            table.update(f'{script} {item}' for item in inputs)
            if not inputs:
                table.add(script)
    # A contract with inputs needs no line without an input.
    with_inputs = {line.split()[0] for line in table if len(line.split()) == 2}
    table = {line for line in table if len(line.split()) == 2 or line not in with_inputs}
    with open(os.path.join(src, INPUTS), 'w', encoding='utf-8') as stream:
        stream.write('\n'.join(header + sorted(table)) + '\n')
    print(f'traced {len(commands)} contracts, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
