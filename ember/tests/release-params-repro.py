#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted MKREPRO release parameters regression.
"""Check that a reproducible build keeps build-host paths out of etc/release.

etc/Makefile writes the "Build settings" of etc/release with the _params
target of etc/Makefile.params. The test runs that target with the real
share/mk files and path-valued settings below a host root. With MKREPRO=yes
no host path may be printed; without MKREPRO the paths stay listed.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

PATHS = ('EXTERNAL_TOOLCHAIN', 'INSTALLWORLDDIR')


def params(make, os_root, root, repro):
    variables = ['MKREPRO=' + repro, 'MAKECONF=/dev/null', 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
                 'DISTRIBVER=11.0', 'NETBSDSRCDIR=' + str(os_root)]
    variables += [f'{name}={root / name.lower()}' for name in PATHS]
    result = subprocess.run([make, '-r', '-m', str(os_root / 'share/mk'), '-f', str(os_root / 'etc/Makefile.params'),
                             *variables, '_params'], cwd=root, capture_output=True, text=True,
                            env={'PATH': str(Path(make).parent) + ':/usr/bin:/bin'})
    if result.returncode != 0:
        raise SystemExit('make _params failed:\n' + result.stderr)
    return result.stdout


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--make', required=True, help='bmake from the selected host prefix')
    cli.add_argument('--os', type=Path, default=Path(__file__).resolve().parents[2])
    args = cli.parse_args()
    os_root = args.os.resolve()
    with tempfile.TemporaryDirectory(prefix='ember-release-params-') as temporary:
        root = Path(temporary).resolve()
        repro = params(args.make, os_root, root, 'yes')
        if str(root) in repro:
            leaked = [line.strip() for line in repro.splitlines() if str(root) in line]
            raise SystemExit('MKREPRO release parameters keep a build-host path: ' + '; '.join(leaked))
        if "MACHINE_ARCH = 'aarch64'" not in repro:
            raise SystemExit('MKREPRO release parameters lost the target settings:\n' + repro)
        plain = params(args.make, os_root, root, 'no')
        for name in PATHS:
            if f"{name} = '{root / name.lower()}'" not in plain:
                raise SystemExit(f'release parameters without MKREPRO no longer list {name}')
    print('PASS: MKREPRO release parameters name no build-host path; plain builds still list '
          + ', '.join(PATHS))


if __name__ == '__main__':
    main()
