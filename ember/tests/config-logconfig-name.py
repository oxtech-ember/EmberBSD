#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted config(1) INCLUDE_CONFIG_FILE regression.
"""Check that config(1) embeds no build-host path with INCLUDE_CONFIG_FILE.

build.sh passes the kernel configuration to config(1) by absolute path. The
test builds config(1) from usr.bin/config as tools/config does, runs it on a
fixture configuration named by an absolute path, and checks config_file.h:
the START and END banners must name the file only, and no host directory may
appear in the text that is compiled into the kernel.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCES = ('files.c', 'hash.c', 'lint.c', 'main.c', 'mkdevsw.c', 'mkheaders.c', 'mkioconf.c',
           'mkmakefile.c', 'mkswap.c', 'pack.c', 'sem.c', 'util.c')


def build_config(args, os_root, work):
    source = os_root / 'usr.bin/config'
    subprocess.run([args.yacc, '-d', '-o', 'gram.c', str(source / 'gram.y')], cwd=work, check=True)
    subprocess.run([args.lex, '-o', 'scan.c', str(source / 'scan.l')], cwd=work, check=True)
    flags = ['-DMAKE_BOOTSTRAP', '-I' + str(args.tooldir / 'include/compat'), '-I' + str(os_root / 'tools/compat'),
             '-DHAVE_NBTOOL_CONFIG_H=1', '-D_FILE_OFFSET_BITS=64', '-I' + str(source), '-I.',
             '-I' + str(os_root / 'usr.bin/cksum'), '-w']
    files = [str(source / name) for name in SOURCES] + ['gram.c', 'scan.c', str(os_root / 'usr.bin/cksum/crc.c')]
    subprocess.run([args.cc, *flags, *files, '-L' + str(args.tooldir / 'lib'), '-lnbcompat', '-o', 'config'],
                   cwd=work, check=True)
    return work / 'config'


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--cc', required=True, help='host C compiler')
    cli.add_argument('--yacc', required=True, help='nbyacc from the tool directory')
    cli.add_argument('--lex', required=True, help='nblex from the tool directory')
    cli.add_argument('--tooldir', type=Path, required=True, help='tool directory with libnbcompat')
    cli.add_argument('--os', type=Path, default=Path(__file__).resolve().parents[2])
    args = cli.parse_args()
    os_root = args.os.resolve()
    with tempfile.TemporaryDirectory(prefix='ember-config-name-') as temporary:
        work = Path(temporary).resolve()
        config = build_config(args, os_root, work)
        sys_root = work / 'host-root/sys'
        (sys_root / 'arch/fix/conf').mkdir(parents=True)
        (sys_root / 'conf').mkdir()
        (sys_root / 'conf/files').write_text('version 20180827\nmaxpartitions 8\nmaxusers 2 8 64\n')
        (sys_root / 'conf/Makefile.kern.inc').write_text('')
        (sys_root / 'arch/fix/conf/files.fix').write_text('include "conf/files"\n')
        (sys_root / 'arch/fix/conf/Makefile.fix').write_text('%INCLUDES\n%OBJS\n%CFILES\n%SFILES\n%LOAD\n%RULES\n')
        conffile = sys_root / 'arch/fix/conf/FIXTURE'
        conffile.write_text('machine fix\noptions INCLUDE_CONFIG_FILE\nmaxusers 8\n'
                            'config netbsd root on ? type ?\n')
        build = work / 'build'
        subprocess.run([str(config), '-s', str(sys_root), '-b', str(build), str(conffile)],
                       cwd=work, check=True, capture_output=True)
        text = (build / 'config_file.h').read_text()
        embedded = [line for line in text.splitlines() if line.startswith('"_CFG_')]
        leaked = [line for line in embedded if str(work) in line]
        if leaked:
            raise SystemExit('config_file.h embeds a build-host path: ' + '; '.join(leaked))
        for banner in ('START', 'END'):
            if f'"_CFG_### {banner} CONFIG FILE \\"FIXTURE\\"\\n"' not in embedded:
                raise SystemExit(f'config_file.h has no {banner} banner naming FIXTURE:\n' + text)
    print('PASS: config(1) names an absolute configuration by its file name in config_file.h')


if __name__ == '__main__':
    main()
