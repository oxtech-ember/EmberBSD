#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile actual libgnumalloc sources and exercise its core allocator API.

The target inventory is read from the owning Makefile. Native public symbols
are renamed so host libc keeps its allocator; __default_morecore is supplied
by a bounded arena, while production morecore.c is target-compiled.
Select native compiler/linker flags with --cflags-json. --sanitize adds
ASan/UBSan. Runtime coverage is allocation, zero fill, growth, shrink,
alignment, freeing, exhausted backing storage and signed morecore calls.
Relocating/debug public APIs
are target-compiled but not covered by this native runtime PASS.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True, type=Path)
    parser.add_argument('--target', required=True)
    parser.add_argument('--sysroot', required=True, type=Path)
    parser.add_argument('--cflags-json', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if not args.cc.is_absolute() or not args.cc.is_file():
        parser.error('--cc must select an existing absolute compiler')
    if not args.sysroot.is_absolute() or not args.sysroot.is_dir():
        parser.error('--sysroot must select existing absolute target headers')
    flags = json.loads(args.cflags_json.read_text())
    if not isinstance(flags, list) or not all(isinstance(v, str) for v in flags):
        parser.error('--cflags-json must contain an argv array')
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    root = Path(__file__).resolve().parents[2]
    library = root / 'external/gpl2/libmalloc'
    makefile = (library / 'lib/Makefile').read_text().replace('\\\n', ' ')
    names = []
    for value in re.findall(r'^SRCS\+?=\s*(.*)$', makefile, re.MULTILINE):
        names.extend(value.split('#', 1)[0].split())
    if not names or any(not re.fullmatch(r'[\w-]+\.c', name) for name in names):
        raise RuntimeError('unsupported libgnumalloc source inventory')
    sources = []
    for name in names:
        choices = [library / area / name for area in ('lib', 'dist')]
        found = [source for source in choices if source.is_file()]
        if len(found) != 1:
            raise RuntimeError('source must have one owner: ' + name)
        sources.append(found[0])
    commands = []

    def run(argv):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=90)
        commands.append(dict(argv=argv, status=result.returncode,
                             output=result.stdout + result.stderr))
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    common = ['-std=gnu11', '-O2', '-Werror', '-Wdeprecated-non-prototype',
              '-DSTDC_HEADERS', '-DHAVE_STDLIB_H', '-DHAVE_UNISTD_H',
              '-I' + str(library / 'dist'), '-I' + str(library / 'lib')]
    for source in sources:
        run([str(args.cc), '--no-default-config', '--target=' + args.target,
             '--sysroot=' + str(args.sysroot), *common, '-fPIC', '-c', str(source),
             '-o', str(work / (source.stem + '.o'))])
    print(str(len(sources)) + ' actual target translation units: PASS', flush=True)
    exports = (library / 'lib/gnumalloc.expsym').read_text().split()
    if any(not re.fullmatch(r'\w+', name) for name in exports):
        raise RuntimeError('unsupported exported symbol')
    rename = ['-D' + name + '=ember_gnumalloc_' + name for name in exports]
    native_sources = [str(source) for source in sources if source.name not in ('morecore.c', 'ralloc.c')]
    fixture = root / 'ember/tests/gnumalloc-prototypes-fixture.c'
    modes = [('native', [])]
    if args.sanitize:
        modes.append(('sanitized', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']))
    for name, extra in modes:
        binary = work / name
        run([str(args.cc), *flags, *common, '-DBSD4_2', *rename, *extra,
             *native_sources, str(fixture), '-o', str(binary)])
        run([str(binary)])
        print(name + ' core allocator: PASS', flush=True)


if __name__ == '__main__':
    main()
