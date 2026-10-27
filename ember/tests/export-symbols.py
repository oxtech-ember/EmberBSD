#!/usr/bin/env python3
"""Exercise the real bsd.lib.mk symbol recipe with selected native tools."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
for name in ('work', 'bmake', 'clang', 'nm', 'readelf', 'shell', 'utility-path'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--source', required=True, type=Path, help='path to bsd.lib.mk')
p.add_argument('--ld', required=True, type=Path, help='selected target linker')
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
env = {'PATH': str(a.utility_path), 'LC_ALL': 'C'}
def run(argv, *, check=True):
    return subprocess.run(list(map(str, argv)), cwd=a.work, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check)
(a.work/'fixture.s').write_text('''
.text
.globl old_impl, new_impl
.type old_impl,%function
.type new_impl,%function
old_impl: bl needed
ret
new_impl: ret
.symver old_impl,probe@VER_1
.symver new_impl,probe@@VER_2
''')
(a.work/'versions.map').write_text('VER_1 {};\nVER_2 {} VER_1;\n')
(a.work/'dependency.s').write_text(
    '.text\n.globl needed\n.type needed,%function\nneeded: ret\n')
(a.work/'dependency.map').write_text('NEEDED_1 { global: needed; };\n')
run([a.clang, '--target=aarch64--netbsd', '-c', 'dependency.s', '-o', 'dependency.o'])
run([a.ld, '-shared', '--soname=dependency.so', '--version-script=dependency.map',
     'dependency.o', '-o', 'dependency.so'])
run([a.clang, '--target=aarch64--netbsd', '-c', 'fixture.s', '-o', 'fixture.o'])
run([a.ld, '-shared', '--version-script=versions.map', 'fixture.o', 'dependency.so',
     '-o', 'fixture.so'])
src = a.source.read_text()
rule = src.split('${_LIB.so.full}.actsym: ${_LIB.so.full}\n', 1)[1].split('CLEANFILES+=', 1)[0]
filters = [line for line in src.splitlines() if line.startswith('_EXPSYM_FILTER=')]
assert len(filters) == 2
makefile = '.SHELL: name=sh path=' + str(a.shell) + '\n_MKTARGET_CREATE=\nMV=mv -f\n' + filters[0] + '\nfixture.actsym: fixture.so\n' + rule
(a.work/'Makefile').write_text(makefile)
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.nm), 'READELF='+str(a.readelf), 'fixture.actsym'])
output = (a.work/'fixture.actsym').read_text().splitlines()
assert 'probe@VER_1' in output and 'probe@@VER_2' in output, output
assert 'VER_1' in output and 'VER_2' in output, output
assert 'NEEDED_1' not in output and 'dependency.so' not in output, output
assert output.count('VER_1') == output.count('VER_2') == 1, output
# The normal no-version-map filter must keep versions and drop ignored names.
filtered = makefile.replace(filters[0], filters[1]).replace(
    'fixture.actsym: fixture.so',
    '_EXPSYM_IGNORE=old_impl new_impl\nfixture.actsym: fixture.so')
(a.work/'Makefile').write_text(filtered)
os.utime(a.work/'fixture.actsym', (1, 1))
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.nm), 'READELF='+str(a.readelf), 'fixture.actsym'])
output = (a.work/'fixture.actsym').read_text().splitlines()
assert 'probe@VER_1' in output and 'probe@@VER_2' in output, output
assert 'old_impl' not in output and 'new_impl' not in output, output
# A versionless DSO and fully ignored symbols produce a valid empty list.
(a.work/'plain.s').write_text('.text\n.globl plain\n.type plain,%function\nplain: ret\n')
run([a.clang, '--target=aarch64--netbsd', '-c', 'plain.s', '-o', 'plain.o'])
run([a.ld, '-shared', 'plain.o', '-o', 'fixture.so'])
(a.work/'empty-nm').write_text('#!' + str(a.shell) + '\nprintf "000 T old_impl\\n"\n')
(a.work/'empty-nm').chmod(0o755)
os.utime(a.work/'fixture.actsym', (1, 1))
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.work/'empty-nm'), 'READELF='+str(a.readelf), 'fixture.actsym'])
assert (a.work/'fixture.actsym').read_text() == ''
# A producer that writes a partial result and fails must not replace old output.
(a.work/'bad-nm').write_text('#!' + str(a.shell) + '\nprintf "000 T partial\\n"\nexit 9\n')
(a.work/'bad-nm').chmod(0o755)
(a.work/'fixture.actsym').write_text('previous\n')
os.utime(a.work/'fixture.actsym', (1, 1))
result = run([a.bmake, '-f', 'Makefile', 'NM='+str(a.work/'bad-nm'), 'READELF='+str(a.readelf), 'fixture.actsym'], check=False)
assert result.returncode != 0, result.stdout
assert (a.work/'fixture.actsym').read_text() == 'previous\n'
for suffix in ('raw', 'names', 'versions', 'filtered', 'tmp'):
    assert not (a.work/('fixture.actsym.' + suffix)).exists(), suffix
print('versioned symbols and producer failure: passed')

# A readelf failure after partial output must not replace the old result.
(a.work/'bad-readelf').write_text('#!' + str(a.shell) +
    '\nprintf "Version definition section partial\\n"\nexit 9\n')
(a.work/'bad-readelf').chmod(0o755)
(a.work/'fixture.actsym').write_text('previous\n')
os.utime(a.work/'fixture.actsym', (1, 1))
result = run([a.bmake, '-f', 'Makefile', 'NM='+str(a.nm),
              'READELF='+str(a.work/'bad-readelf'), 'fixture.actsym'], check=False)
assert result.returncode != 0
assert (a.work/'fixture.actsym').read_text() == 'previous\n'
for suffix in ('raw', 'names', 'versions', 'filtered', 'tmp'):
    assert not (a.work/('fixture.actsym.' + suffix)).exists(), suffix
print('readelf failure preserves previous symbols: passed')
