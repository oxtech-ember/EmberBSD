#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Select the kernel contracts that a set of changed files can affect.

usage: kernel-contracts-select.py SOURCE_ROOT CHANGED_FILES SELECTION

CHANGED_FILES has one path per line, relative to SOURCE_ROOT. SELECTION
gets one contract path per line, in kernel-contracts.sh order.
"""
import os
import re
import sys

INPUTS = 'ember/tools/kernel-contracts-inputs.txt'
# A change to the selection itself can change each result.
RUNNER = {'.github/workflows/kernel-contracts.yml', 'ember/check-kernel.sh',
          'ember/tools/kernel-contracts.sh', 'ember/tools/kernel-contracts-select.py', INPUTS}


def contracts(src):
    """Return the contract scripts in dispatch order."""
    found = []
    path = os.path.join(src, 'ember/tools/kernel-contracts.sh')
    with open(path, encoding='utf-8') as stream:
        for line in stream:
            match = re.search(r'"\$src/(ember/[^"]+)"', line)
            if match and match.group(1) not in found:
                found.append(match.group(1))
    return found


def inputs(src):
    """Return the recorded inputs of each contract."""
    table = {}
    with open(os.path.join(src, INPUTS), encoding='utf-8') as stream:
        for number, line in enumerate(stream, 1):
            fields = line.split()
            if not fields or fields[0].startswith('#'):
                continue
            if len(fields) > 2:
                sys.exit(f'{INPUTS}:{number}: expected CONTRACT [INPUT]')
            table.setdefault(fields[0], set()).update(fields[1:])
    return table


def affected(change, recorded):
    for item in recorded:
        if item.endswith('/**'):
            if change.startswith(item[:-2]):
                return True
        elif item.endswith('/'):
            if os.path.dirname(change) + '/' == item:
                return True
        elif change == item:
            return True
    return False


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[2])
    src, changed, selection = sys.argv[1:]
    with open(changed, encoding='utf-8') as stream:
        changes = {line.strip() for line in stream if line.strip()}
    scripts = contracts(src)
    table = inputs(src)
    if changes & RUNNER:
        selected = scripts
    else:
        selected = [script for script in scripts
                    if script not in table or script in changes or
                    any(affected(change, table[script]) for change in changes)]
    with open(selection, 'w', encoding='utf-8') as stream:
        stream.writelines(script + '\n' for script in selected)
    print(f'selected {len(selected)} of {len(scripts)} kernel contracts '
          f'for {len(changes)} changed files')


if __name__ == '__main__':
    main()
