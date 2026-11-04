#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise actual compat installation rules with the selected host xinstall."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--bmake', required=True)
parser.add_argument('--install', required=True)
parser.add_argument('--os-make', required=True)
parser.add_argument('--host-prefix', required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent / 'Makefile'
text = source.read_text()
for override, expected in (([], '-C'), (['HOST_INSTALL_FILE=custom-install'], 'custom-install'),
                           (['INSTALL=custom-install'], 'custom-install -c -p -r')):
    result = subprocess.run([args.os_make, '-C', str(source.parent), *override,
                             '-V', '${HOST_COMPAT_INSTALL_FILE}'], text=True,
                            capture_output=True, check=True)
    assert result.stdout.strip().endswith(expected), result.stdout
fragment = text.split('# Install rules\n', 1)[1].split('# bsd.hostlib.mk wants', 1)[0]
selector = text.split('# Only our host xinstall', 1)[1].split('# Run ', 1)[0]
selector = '# Only our host xinstall' + selector
with tempfile.TemporaryDirectory(prefix='compat-install-') as temp:
    root = Path(temp)
    tools = root / 'tools'
    env = dict(os.environ, PATH=str(Path(args.host_prefix) / 'gnu/bin') + ':' + str(Path(args.host_prefix) / 'bin'))
    sources = []
    for name in ('first', 'second'):
        work = root / name
        (work / 'include/sys').mkdir(parents=True)
        for filename, content in (('nbtool_config.h', b'header\n'), ('defs.mk', b'defs\n'), ('include/sys/extra.h', b'extra\n')):
            (work / filename).write_bytes(content)
        (work / 'xinstall').symlink_to(args.install)
        (work / 'Makefile').write_text('TOOLDIR= ' + str(tools) + '\nHOSTLIB= nbcompat\nINCFILES= nbtool_config.h\nINCSUBDIRS= sys\n_MKTARGET_INSTALL= @:\nINSTALL_OBJ= ' + str(work) + '\nINSTALL= ${INSTALL_OBJ}/xinstall\nCOPY= -c\nPRESERVE= -p\nRENAME= -r\nHOST_INSTALL_FILE= ${INSTALL} ${COPY} ${PRESERVE} ${RENAME}\nHOST_INSTALL_DIR= mkdir -p\n' + fragment + selector)
        sources.append(work)
    def install(work):
        result = subprocess.run([args.bmake, '-r', '-f', str(work / 'Makefile'), 'includes', 'install.defs.mk'], cwd=work, env=env, text=True, capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
    selected = subprocess.run([args.bmake, '-r', '-f', str(sources[0] / 'Makefile'),
                               '-V', '${HOST_COMPAT_INSTALL_FILE}'], cwd=sources[0],
                              env=env, text=True, capture_output=True, check=True)
    assert selected.stdout.strip().endswith(' -C'), selected.stdout
    override = subprocess.run([args.bmake, '-r', '-f', str(sources[0] / 'Makefile'),
                               'HOST_INSTALL_FILE=custom-install', '-V', '${HOST_COMPAT_INSTALL_FILE}'],
                              cwd=sources[0], env=env, text=True, capture_output=True, check=True)
    assert override.stdout.strip() == 'custom-install', override.stdout
    install(sources[0])
    outputs = [tools / 'include/compat/nbtool_config.h', tools / 'include/compat/sys/extra.h', tools / 'share/compat/defs.mk']
    before = [(p.stat().st_ino, p.stat().st_mtime_ns) for p in outputs]
    for name in ('nbtool_config.h', 'include/sys/extra.h', 'defs.mk'):
        p = sources[1] / name
        st = p.stat()
        os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns + 10_000_000_000))
    install(sources[1])
    assert [(p.stat().st_ino, p.stat().st_mtime_ns) for p in outputs] == before
    for name in ('nbtool_config.h', 'include/sys/extra.h', 'defs.mk'):
        p = sources[1] / name
        p.write_bytes(b'changed\n')
        st = p.stat()
        os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns + 20_000_000_000))
    install(sources[1])
    assert all(p.read_bytes() == b'changed\n' for p in outputs)
    # Explicit side effects and uncertain destination metadata must retain
    # the ordinary install behavior, even when bytes match.
    original = root / 'original'
    original.write_bytes(b'payload')
    target = root / 'target'
    def call(*options):
        subprocess.run([args.install, '-c', '-p', '-r', *options,
                        str(original), str(target)], check=True, env=env)
    call('-C')
    target.chmod(0o600)
    call('-C')
    assert target.stat().st_mode & 0o7777 == 0o755
    target.unlink()
    linked = root / 'linked'
    linked.write_bytes(b'payload')
    target.symlink_to(linked)
    call('-C')
    assert not target.is_symlink() and target.read_bytes() == b'payload'
    hardlink = root / 'hardlink'
    os.link(target, hardlink)
    call('-C')
    assert target.stat().st_ino != hardlink.stat().st_ino
    call('-C', '-B', '.backup')
    assert (root / 'target.backup').read_bytes() == b'payload'
    before_no_compare = target.stat().st_ino
    call()
    assert target.stat().st_ino != before_no_compare
    log = root / 'metalog'
    before_log = target.stat().st_ino
    call('-C', '-M', str(log))
    assert log.stat().st_size > 0 and target.stat().st_ino != before_log
    before_unprivileged = target.stat().st_ino
    call('-C', '-U', '-M', str(root / 'unprivileged.log'))
    assert target.stat().st_ino != before_unprivileged
print('compat headers, metadata fallback and install options PASS')
