#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Translate upstream's configured ELF version map into bsd.lib expected exports."""
import re
import sys
from pathlib import Path


def expected(path):
    node = None
    exports = set()
    symbols = set()
    in_global = False
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        match = re.fullmatch(r'(OPENSSL_[0-9]+(?:\.[0-9]+)+) \{', line)
        if match:
            if node is not None or match[1] in exports:
                raise ValueError('duplicate or nested version node')
            node = match[1]
            exports.add(node)
        elif line == 'global:' and node:
            in_global = True
        elif line == 'local: *;' and node:
            in_global = False
        elif re.fullmatch(r'\}(?: OPENSSL_[0-9]+(?:\.[0-9]+)+)?;', line) and node:
            node = None
            in_global = False
        else:
            match = re.fullmatch(r'([A-Za-z_][A-Za-z0-9_]*);', line)
            if not match or not node or not in_global or match[1] in symbols:
                raise ValueError('unexpected or duplicate map entry: ' + line)
            symbols.add(match[1])
            exports.add(match[1] + '@@' + node)
    if node or not symbols:
        raise ValueError('incomplete or empty version map')
    return ''.join(name + '\n' for name in sorted(exports))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: map-exports.py VERSION_MAP OUTPUT')
    Path(sys.argv[2]).write_text(expected(sys.argv[1]))
