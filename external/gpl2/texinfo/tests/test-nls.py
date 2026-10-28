#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check installation of the existing OS Texinfo locale paths."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--install', type=Path, required=True)
a = p.parse_args()
prefix = a.prefix.resolve(strict=True)
source = a.source.resolve(strict=True)
install = a.install.resolve(strict=True)
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    work = Path(temporary)
    dest = work / 'dest'
    obj = work / 'obj'
    obj.mkdir()
    (dest / 'usr/share/misc').mkdir(parents=True)
    locales = 'cs da de de_AT eo fr he hr ja nb nl no pl ro ru sv tr zh_CN zh_TW'.split()
    for language in locales:
        (dest / 'usr/share/locale' / language / 'LC_MESSAGES').mkdir(parents=True)
    env = dict(os.environ, MAKEOBJDIR=str(obj), PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'))
    cmd = [str(prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'), '-C',
           str(root / 'external/gpl2/texinfo'), 'MAKECONF=/dev/null', '_SRC_TOP_=',
           'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
           'USETOOLS=yes', 'TOOLDIR=' + str(work / 'tools'), 'EMBER_TEXINFO_SOURCE=' + str(source),
           'MKNLS=yes', 'DESTDIR=' + str(dest), 'INSTALL=' + str(install),
           'INSTPRIV=-U -M ' + str(work / 'METALOG') + ' -D ' + str(dest) + ' -h sha256']
    disabled = [arg for arg in cmd if arg != 'MKNLS=yes'] + ['MKNLS=no', '-V', '${FILES}']
    result = subprocess.run(disabled, env=env, capture_output=True, text=True, check=True)
    if '.gmo' in result.stdout:
        raise SystemExit('disabled NLS still installs catalogs')
    disabled_share = [*cmd, 'MKSHARE=no', '-V', '${FILES}']
    result = subprocess.run(disabled_share, env=env, capture_output=True, text=True, check=True)
    if any(name in result.stdout for name in ('epsf.tex', 'texinfo.tex', 'texinfo.dtd')):
        raise SystemExit('disabled share still installs Texinfo data')
    for targets in (('de_AT.gmo', 'no.gmo'), ('filesinstall',)):
        result = subprocess.run([*cmd, *targets], env=env, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
    assert (work / 'METALOG').is_file()
    for name, directory in (('epsf.tex', 'doc'), ('texinfo.tex', 'doc'),
                            ('texinfo.dtd', 'util')):
        if (dest / 'usr/share/misc' / name).read_bytes() != (source / directory / name).read_bytes():
            raise SystemExit('wrong installed Texinfo data: ' + name)
    for language in locales:
        origin = {'de_AT': 'de', 'no': 'nb'}.get(language, language)
        installed = dest / 'usr/share/locale' / language / 'LC_MESSAGES/texinfo.mo'
        if installed.read_bytes() != (source / 'po' / (origin + '.gmo')).read_bytes():
            raise SystemExit('wrong installed Texinfo locale: ' + language)
print('Texinfo existing NLS install paths: PASS')
