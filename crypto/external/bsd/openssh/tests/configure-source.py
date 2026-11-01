#!/usr/bin/env python3
"""Exercise target configure view reuse, failure and concurrent consumers."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--work-dir', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'tools/configure-source.py'
with tempfile.TemporaryDirectory(dir=args.work_dir) as tmp:
    root = Path(tmp)
    source = root / 'source'
    source.mkdir()
    (source / 'm4').mkdir()
    (source / 'configure.ac').write_text('original macros\n')
    (source / 'aclocal.m4').write_text('aclocal\n')
    (source / 'm4' / 'source.m4').write_text('m4\n')
    (source / 'config.h.in').write_text('header\n')
    (source / 'configure').write_text('original configure\n')
    (source / 'configure.new').write_text('reserved sentinel\n')
    (source / 'autom4te.cache').mkdir()
    (source / 'autom4te.cache' / 'sentinel').write_text('immutable\n')
    (source / 'ssh.c').write_text('original source\n')
    for name in ('Makefile.in', 'buildpkg.sh.in', 'opensshd.init.in',
                 'openssh.xml.in', 'openbsd-compat/Makefile.in',
                 'openbsd-compat/regress/Makefile.in', 'survey.sh.in',
                 '.depend', 'config.guess', 'config.sub', 'ssh.1'):
        item = source / name
        item.parent.mkdir(parents=True, exist_ok=True)
        item.write_text(name + '\n')
    source_before = {p.relative_to(source): p.read_bytes() for p in source.rglob('*') if p.is_file()}
    os_source = root / 'os-source'
    os_source.mkdir()
    obj = root / 'objects'
    log = root / 'calls'
    autoconf = root / 'autoconf'
    autoconf.write_text(f'''#!{sys.executable}
import os, pathlib, sys
assert not any(v in os.environ for v in ('AUTOM4TE','AUTOM4TE_CFG','M4','PERL5LIB','PERL5OPT'))
p = pathlib.Path(os.environ['TEST_LOG'])
with p.open('a') as f: f.write('autoconf\\n')
if os.environ.get('TEST_AUTOCONF_FAIL'): sys.exit(1)
pathlib.Path(sys.argv[-1]).write_text('generated configure\\n')
''')
    make = root / 'make'
    make.write_text(f'''#!{sys.executable}
import os, pathlib, sys, time
obj = pathlib.Path(next(a.split('=',1)[1] for a in sys.argv if a.startswith('OPENSSH_CONFIG_OBJECT=')))
build = obj / 'build'
build.mkdir(exist_ok=True)
flags = os.environ.get('MAKEFLAGS', '')
assert '-j1' in sys.argv
assert '-J' not in flags and '-j' not in flags and 'TEST_OVERRIDE=yes' in flags
active = obj / '.test-active'
fd = os.open(active, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
os.close(fd)
try:
    with pathlib.Path(os.environ['TEST_LOG']).open('a') as f:
        f.write('make-' + ('cold' if not (build / 'Makefile').exists() else 'warm') + '\\n')
    time.sleep(0.1)
    (build / 'Makefile').write_text('makefile\\n')
    (build / 'config.h').write_text('config\\n')
finally:
    active.unlink()
''')
    autoconf.chmod(0o755)
    make.chmod(0o755)
    command = [sys.executable, '-B', str(helper), '--source', str(source),
               '--os-source', str(os_source), '--object', str(obj),
               '--autoconf', str(autoconf), '--make', str(make), '--makefile-dir', str(root)]
    env = dict(os.environ, TEST_LOG=str(log), MAKEFLAGS='-j 10 -J 15,16 TEST_OVERRIDE=yes',
               AUTOM4TE='/external/autom4te', M4='/external/m4', PERL5LIB='/external/lib')
    first = subprocess.Popen(command, env=env)
    second = subprocess.Popen(command, env=env)
    assert first.wait() == 0 and second.wait() == 0
    assert log.read_text().splitlines() == ['autoconf', 'make-cold', 'make-warm']
    assert not (obj / 'source-view' / 'configure.new').is_symlink()
    assert not (obj / 'source-view' / 'autom4te.cache').is_symlink()
    (source / 'new-source.c').write_text('new\n')
    subprocess.run(command, env=env, check=True)
    assert (obj / 'source-view' / 'new-source.c').is_symlink()
    (source / 'new-source.c').unlink()
    subprocess.run(command, env=env, check=True)
    assert not (obj / 'source-view' / 'new-source.c').exists()
    assert log.read_text().splitlines().count('autoconf') == 1
    target_lib = root / 'libkrb5.so'
    command += ['--target-lib', str(target_lib)]
    subprocess.run(command, env=env, check=True)
    missing_inputs = (obj / '.configure-source-inputs').read_text()
    target_lib.write_bytes(b'first library')
    subprocess.run(command, env=env, check=True)
    first_inputs = (obj / '.configure-source-inputs').read_text()
    assert first_inputs != missing_inputs
    first_stat = target_lib.stat()
    target_lib.write_bytes(b'other library')
    os.utime(target_lib, ns=(first_stat.st_atime_ns, first_stat.st_mtime_ns))
    subprocess.run(command, env=env, check=True)
    changed_inputs = (obj / '.configure-source-inputs').read_text()
    assert changed_inputs != first_inputs
    replacement = root / 'replacement.so'
    replacement.write_bytes(b'other library')
    os.utime(replacement, ns=(first_stat.st_atime_ns, first_stat.st_mtime_ns))
    replacement.replace(target_lib)
    subprocess.run(command, env=env, check=True)
    assert (obj / '.configure-source-inputs').read_text() != changed_inputs
    target_lib.unlink()
    subprocess.run(command, env=env, check=True)
    assert (obj / '.configure-source-inputs').read_text() == missing_inputs
    assert log.read_text().splitlines().count('autoconf') == 1
    old_inputs = (obj / '.configure-source-inputs').read_text()
    (source / 'Makefile.in').write_text('changed template\n')
    subprocess.run(command, env=env, check=True)
    assert (obj / '.configure-source-inputs').read_text() != old_inputs
    assert log.read_text().splitlines().count('autoconf') == 1
    source_before[Path('Makefile.in')] = b'changed template\n'
    (obj / 'build' / 'config.h').unlink()
    subprocess.run(command, env=env, check=True)
    assert log.read_text().splitlines()[-1] == 'make-cold'
    (source / 'configure.ac').write_text('changed macros\n')
    failing = dict(env, TEST_AUTOCONF_FAIL='1')
    assert subprocess.run(command, env=failing, capture_output=True).returncode != 0
    assert log.read_text().splitlines()[-1] == 'autoconf'
    subprocess.run(command, env=env, check=True)
    assert log.read_text().splitlines()[-2:] == ['autoconf', 'make-warm']
    invalid = command.copy()
    invalid[invalid.index('--object') + 1] = str(source / 'forbidden')
    assert subprocess.run(invalid, env=env, capture_output=True).returncode != 0
    clean = [sys.executable, '-B', str(helper), '--clean', '--object', str(obj),
             '--objdir', str(root), '--os-source', str(os_source), '--clean-helper',
             str(Path(__file__).resolve().parents[5] / 'share/mk/waf-clean.py')]
    outside = root / 'outside'
    outside.mkdir()
    (outside / 'sentinel').write_text('preserved')
    link = root / 'linked-object'
    link.symlink_to(outside, target_is_directory=True)
    bad_clean = clean.copy()
    bad_clean[bad_clean.index('--object') + 1] = str(link)
    assert subprocess.run(bad_clean, env=env, capture_output=True).returncode != 0
    assert (outside / 'sentinel').read_text() == 'preserved'
    subprocess.run(clean, env=env, check=True)
    assert not obj.exists()
    subprocess.run(command, env=env, check=True)
    assert (obj / 'build' / 'config.h').is_file()
    source_after = {p.relative_to(source): p.read_bytes() for p in source.rglob('*') if p.is_file()}
    source_before[Path('configure.ac')] = b'changed macros\n'
    assert source_before == source_after
print('OpenSSH target configure locking, reuse and recovery PASS')
