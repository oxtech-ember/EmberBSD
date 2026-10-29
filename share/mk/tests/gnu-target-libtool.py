#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Verify target libtool selection and changed-tool invalidation."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('host-prefix', 'work-dir'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
host = a.host_prefix
with tempfile.TemporaryDirectory(dir=a.work_dir) as tmp:
    work = Path(tmp)
    source = work / 'source'
    source.mkdir()
    (source / 'configure').write_text('''mkdir -p host/sub nested header
cat > header/Makefile <<'HEADER'
all:
\tprintf unexpected > unexpected
header-only:
\tprintf generated > generated
HEADER
cat > host/sub/Makefile <<'HOSTSUB'
LIBTOOL=host-libtool
all:
\tprintf '%s\\n' '$(LIBTOOL)' > ../host-selected
\tprintf '%s\\n' '$(CFLAGS)' > ../host-flags
HOSTSUB
cat > nested/Makefile <<'NESTED'
LIBTOOL=wrong-libtool
all:
\t$(LIBTOOL) --tag=CC --mode=link
\tprintf '%s\\n' '$(CFLAGS)' > nested-flags
NESTED
cat > host/Makefile <<'HOST'
all:
\t$(MAKE) -C sub all
HOST
cat > Makefile <<'END'
all:
\t$(MAKE) -C nested all
\tprintf object > object

distclean:
\trm -f Makefile selected object marker
END
''')
    tool = work / 'selected libtool'
    tool.write_text('#!' + str(host / 'bin/mksh') + '\nprintf \"%s\\n\" \"$@\" > selected\n')
    tool.chmod(0o755)
    makefile = work / 'Makefile'
    makefile.write_text('''GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= host
GNU_HOST_TRIPLE= target
HOST_SH= %s
HOST_GMAKE= %s
GNU_TARGET_LIBTOOL= %s
CFLAGS= -DSELECTED=1
HOST_CFLAGS= -DHOST_ONLY=1
GNU_CONFIGURE_CFLAGS= -DPROBE=1
GNU_HOST_MAKE_DIRS= host
GNU_MAKE_DIRS= header .
GNU_MAKE_TARGETS.header= header-only
.include "%s"
all: gnu-target-build
''' % (source, work / 'build', host / 'bin/mksh', host / 'bin/gmake',
       tool, root / 'share/mk/bsd.gnutarget.mk'))
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'),
               TMPDIR=str(a.work_dir), MAKEOVERRIDES='HOSTILE=1')
    cmd = [str(host / 'bin/bmake'), '-r', '-f', str(makefile), 'all']
    result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    build = work / 'build'
    if (build / 'header/generated').read_text() != 'generated' or (build / 'header/unexpected').exists():
        raise SystemExit('per-directory target selection failed')
    if (build / 'nested/selected').read_text().splitlines() != ['--tag=CC', '--mode=link']:
        raise SystemExit('selected libtool was not executed with its arguments')
    if (build / 'host/host-selected').read_text().strip() != 'host-libtool':
        raise SystemExit('separate host phase received target libtool')
    if (build / 'host/host-flags').read_text().strip() != '-DHOST_ONLY=1':
        raise SystemExit('recursive host phase lost its host-only flags')
    if (build / 'nested/nested-flags').read_text().strip() != '-DSELECTED=1':
        raise SystemExit('recursive target make lost actual build flags')
    (build / 'marker').write_text('old object context')
    result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if not (build / 'marker').exists():
        raise SystemExit('unchanged libtool discarded reusable objects')
    inputs = build / '.configure-inputs'
    lines = inputs.read_text().splitlines(keepends=True)
    inputs.write_text(''.join(line for line in lines if line.strip() not in
                              ('target-make-overrides=recursive', 'host-make-overrides=recursive')))
    result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if (build / 'marker').exists():
        raise SystemExit('old make override semantics left stale objects')
    (build / 'marker').write_text('old tool context')
    tool.write_text(tool.read_text() + '# changed\n')
    result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if (build / 'marker').exists():
        raise SystemExit('changed libtool left stale objects')
    if (build / '.configure-libtool').read_text() != tool.read_text():
        raise SystemExit('libtool identity was not updated')
    expected = tool.read_text()
    tool.unlink()
    failed = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if failed.returncode == 0 or (build / '.configure-libtool').read_text() != expected:
        raise SystemExit('missing libtool did not fail without changing snapshot')
print('GNU target libtool selection and invalidation: PASS')
