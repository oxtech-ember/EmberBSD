#!/usr/bin/env python3
"""Exercise the real bsd.lib.mk symbol recipe with selected native tools."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
for name in ('work', 'bmake', 'clang', 'nm', 'shell', 'utility-path'):
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
old_impl: ret
new_impl: ret
.symver old_impl,probe@VER_1
.symver new_impl,probe@@VER_2
''')
(a.work/'versions.map').write_text('VER_1 {};\nVER_2 {} VER_1;\n')
run([a.clang, '--target=aarch64--netbsd', '-c', 'fixture.s', '-o', 'fixture.o'])
run([a.ld, '-shared', '--version-script=versions.map', 'fixture.o', '-o', 'fixture.so'])
src = a.source.read_text()
rule = src.split('${_LIB.so.full}.actsym: ${_LIB.so.full}\n', 1)[1].split('CLEANFILES+=', 1)[0]
filters = [line for line in src.splitlines() if line.startswith('_EXPSYM_FILTER=')]
assert len(filters) == 2
makefile = '.SHELL: name=sh path=' + str(a.shell) + '\n_MKTARGET_CREATE=\nMV=mv -f\n' + filters[0] + '\nfixture.actsym: fixture.so\n' + rule
(a.work/'Makefile').write_text(makefile)
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.nm), 'fixture.actsym'])
output = (a.work/'fixture.actsym').read_text().splitlines()
assert 'probe@VER_1' in output and 'probe@@VER_2' in output, output
# The normal no-version-map filter must keep versions and drop ignored names.
filtered = makefile.replace(filters[0], filters[1]).replace(
    'fixture.actsym: fixture.so',
    '_EXPSYM_IGNORE=old_impl new_impl\nfixture.actsym: fixture.so')
(a.work/'Makefile').write_text(filtered)
os.utime(a.work/'fixture.actsym', (1, 1))
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.nm), 'fixture.actsym'])
output = (a.work/'fixture.actsym').read_text().splitlines()
assert 'probe@VER_1' in output and 'probe@@VER_2' in output, output
assert 'old_impl' not in output and 'new_impl' not in output, output
# grep returns 1 when every symbol is ignored; that is a valid empty list.
(a.work/'empty-nm').write_text('#!' + str(a.shell) + '\nprintf "000 T old_impl\\n"\n')
(a.work/'empty-nm').chmod(0o755)
os.utime(a.work/'fixture.actsym', (1, 1))
run([a.bmake, '-f', 'Makefile', 'NM='+str(a.work/'empty-nm'), 'fixture.actsym'])
assert (a.work/'fixture.actsym').read_text() == ''
# A producer that writes a partial result and fails must not replace old output.
(a.work/'bad-nm').write_text('#!' + str(a.shell) + '\nprintf "000 T partial\\n"\nexit 9\n')
(a.work/'bad-nm').chmod(0o755)
(a.work/'fixture.actsym').write_text('previous\n')
os.utime(a.work/'fixture.actsym', (1, 1))
result = run([a.bmake, '-f', 'Makefile', 'NM='+str(a.work/'bad-nm'), 'fixture.actsym'], check=False)
assert result.returncode != 0, result.stdout
assert (a.work/'fixture.actsym').read_text() == 'previous\n'
for suffix in ('raw', 'names', 'filtered', 'tmp'):
    assert not (a.work/('fixture.actsym.' + suffix)).exists(), suffix
print('versioned symbols and producer failure: passed')
