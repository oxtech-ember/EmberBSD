#!/usr/bin/env python3
"""Check crunchgen's handling of COMMON, keep, main, and undefined symbols."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(args, cwd):
    return subprocess.run([str(a) for a in args], cwd=cwd, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True).stdout


def main():
    parser = argparse.ArgumentParser()
    for name in ('clang', 'ld', 'nm', 'objcopy', 'readelf', 'crunchgen',
                 'make', 'awk'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--target', required=True)
    args = parser.parse_args()
    tools = {name: Path(getattr(args, name)).absolute()
             for name in ('clang', 'ld', 'nm', 'objcopy', 'readelf',
                          'crunchgen', 'make', 'awk')}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / 'fixture.conf').write_text(
            'progs one two\nspecial one keepsymbols shared\n'
            'special two keepsymbols shared\n')
        run([tools['crunchgen'], '-f', 'fixture.conf'], root)
        for name in ('one', 'two'):
            path = root / name
            path.mkdir()
            source = path / (name + '.c')
            shared = 'int shared;' if name == 'one' else 'extern int shared;'
            source.write_text('int counter; ' + shared + ' extern int outside;\n'
                              'int main(void) { return ++counter + shared + outside; }\n')
            obj, rel = path / (name + '.o'), path / (name + '.ro')
            run([tools['clang'], '--target=' + args.target, '-fcommon', '-c',
                 source, '-o', obj], root)
            run([tools['ld'], '-r', obj, '-o', rel], root)
        run([tools['make'], '-f', 'fixture.mk', 'NM=' + str(tools['nm']),
             'OBJCOPY=' + str(tools['objcopy']), 'AWK=' + str(tools['awk']),
             'one.cro', 'two.cro'], root)
        for name in ('one', 'two'):
            syms = (root / (name + '.cro.syms')).read_text()
            assert 'counter counter$$from$$' + name in syms
            assert 'main _crunched_' + name + '_stub' in syms
            assert 'shared ' not in syms and 'outside ' not in syms
            relocs = run([tools['readelf'], '-r', name + '.cro'], root)
            assert 'counter$$from$$' + name in relocs
        (root / 'outside.c').write_text('int outside = 7;\n')
        run([tools['clang'], '--target=' + args.target, '-c', 'outside.c',
             '-o', 'outside.o'], root)
        run([tools['ld'], '-static', '-e', '_crunched_one_stub', 'one.cro',
             'two.cro', 'outside.o', '-o', 'fixture'], root)
        symbols = run([tools['readelf'], '--wide', '--symbols', 'fixture'], root)
        rows = {line.split()[-1]: line.split() for line in symbols.splitlines()
                if len(line.split()) >= 8}
        names = ('counter$$from$$one', 'counter$$from$$two', 'shared',
                 'outside', '_crunched_one_stub', '_crunched_two_stub')
        for name in names:
            assert name in rows and rows[name][6] != 'UND', (name, rows)
        assert rows[names[0]][1] != rows[names[1]][1]
        assert sum(line.rstrip().endswith(' shared') for line in symbols.splitlines()) == 1
    print('crunch COMMON isolation: PASS')


if __name__ == '__main__':
    main()
