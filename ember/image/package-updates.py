#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Prepare the package update layer for a fresh EmberBSD image."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


CHANNEL = 'https://emberbsd-pkg.oxteam.me'
CLIENT = Path('usr/local/libexec/ember-sdk.pyz')
CONFIG = Path('usr/local/etc/ember-sdk.json')
CA = Path('usr/pkg/share/mozilla-rootcerts/cacert.pem')


def write_config(path, config):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')


def assemble(args, work, root):
    prefix = args.host_prefix.resolve(strict=True)
    sdk = args.sdk_source.resolve(strict=True)
    sys.path.insert(0, str(sdk / 'src'))
    from emberbsd_sdk.elf import read_elf_header
    from emberbsd_sdk.errors import SDKError
    from emberbsd_sdk.targets import TARGETS
    platform = TARGETS[args.arch]
    base = args.base_root.resolve(strict=True)
    if (base / 'usr/pkg/pkgdb').exists():
        raise ValueError('assemble a fresh image before adding other packages')
    for name in ('libexec/ld.elf_so', 'usr/bin/openssl', 'usr/sbin/pkg_info',
                 'usr/sbin/pkg_admin', 'usr/sbin/pkg_add', 'usr/sbin/pkg_delete',
                 'usr/bin/logger', 'usr/bin/pgrep'):
        path = (base / name).resolve(strict=True)
        if not path.is_relative_to(base) or not path.is_file() or not os.access(path, os.X_OK):
            raise ValueError(f'expected an executable in the target base: {name}')
        try:
            elf = read_elf_header(path, machine=platform['elf'])
        except SDKError as exc:
            raise ValueError(str(exc)) from exc
        if not elf or elf['class'] != 64:
            raise ValueError(f'expected a {args.arch} executable: {name}')
    python = prefix / 'bin/python3.15'
    client = root / CLIENT
    client.parent.mkdir(parents=True)
    fetch = [python, '-B', sdk / 'scripts/s3-release.py', '--host-prefix', prefix]
    if args.https_ca:
        fetch.extend(['--tls-ca-file', args.https_ca])
    artifact = f'bootstrap-netbsd-{args.arch}-client'
    subprocess.run([*fetch, 'fetch-artifact', '--url', CHANNEL, '--trust-root',
                    sdk / 'trust/emberbsd-release-root.pem', '--artifact', artifact,
                    '--output', client], check=True)

    target = {
        'target': {'arch': platform['arch'], 'triplet': platform['triplet'], 'abi': 'netbsd-11',
                   'runtime': 'libc++'},
        'prefix': '/usr/pkg', 'pkgdb': '/usr/pkg/pkgdb', 'os_version': '11.0',
    }
    repo = f'netbsd-{args.arch}'
    device = {
        'schema': 'emberbsd.sdk-client.v1', 'url': CHANNEL,
        'host_prefix': '/usr', 'state_dir': '/var/db/emberbsd-sdk',
        'cache_dir': '/var/cache/emberbsd/packages', 'https_ca': '/' + str(CA),
        'repositories': {repo: dict(target, install_mode='native')},
    }
    # pkg_add checks target DSO requirements in the supplied release.
    sysroot = work / 'sysroot'
    for relative in ('lib', 'usr/lib'):
        shutil.copytree(args.base_root / relative, sysroot / relative, symlinks=True)
    host = dict(device, host_prefix=str(prefix), cache_dir=str(args.cache_dir.resolve()),
                state_dir=str(work / 'state'),
                repositories={repo: dict(target, install_mode='sysroot', sysroot=str(sysroot))})
    del host['https_ca']
    if args.https_ca:
        host['https_ca'] = str(args.https_ca.resolve(strict=True))
    config = work / 'stage.json'
    write_config(config, host)
    subprocess.run([python, '-B', client, '--config', config, 'install',
                    '--package', 'lang/python315', '--package', 'security/mozilla-rootcerts'],
                   check=True)
    (sysroot / 'usr/pkg').rename(root / 'usr/pkg')
    for name in (CA, Path('usr/pkg/bin/python3.15')):
        if not (root / name).is_file():
            raise ValueError(f'package installation did not provide {name}')

    write_config(root / CONFIG, device)
    service = 'ember_pkg_update'
    rc = root / 'etc/rc.d' / service
    rc.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(Path(__file__).resolve().parents[2] / 'etc/rc.d' / service, rc)
    rc.chmod(0o555)
    policy = root / 'etc/rc.conf.d' / service
    policy.parent.mkdir(parents=True, exist_ok=True)
    policy.write_text(f'{service}=YES\n', encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-prefix', required=True, type=Path)
    parser.add_argument('--sdk-source', required=True, type=Path)
    parser.add_argument('--arch', required=True, choices=('aarch64', 'amd64'))
    parser.add_argument('--base-root', required=True, type=Path, help='extracted base of the target release')
    parser.add_argument('--cache-dir', required=True, type=Path, help='shared host package cache')
    parser.add_argument('--https-ca', type=Path, help='public HTTPS trust bundle on the build host')
    parser.add_argument('--output', required=True, type=Path, help='new image layer directory')
    args = parser.parse_args()
    output = args.output.absolute()
    if args.cache_dir.resolve().is_relative_to(output.resolve()):
        parser.error('keep the host package cache outside the target layer')
    # The output is a public filesystem layer. The work directory is private.
    os.umask(0o022)
    output.mkdir()
    try:
        with tempfile.TemporaryDirectory(prefix='.pkg-update-', dir=output.parent) as tmp:
            work = Path(tmp)
            root = work / 'root'
            assemble(args, work, root)
            root.replace(output)
    except BaseException:
        output.rmdir()
        raise


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        sys.exit(f'package-updates: {exc}')
