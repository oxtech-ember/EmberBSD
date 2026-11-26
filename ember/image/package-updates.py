#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Enable native package updates in a fresh single-root image."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from os_update_image import check_anchor, check_program, image_policy

TARGETS = {
    'aarch64': ('aarch64', 'aarch64--netbsd'),
    'amd64': ('x86_64', 'x86_64--netbsd'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', required=True, choices=tuple(TARGETS))
    parser.add_argument('--base-root', required=True, type=Path, help='matching extracted base and etc sets')
    parser.add_argument('--output', required=True, type=Path, help='new personalization layer')
    args = parser.parse_args()
    root, output = args.base_root.resolve(strict=True), args.output.absolute()
    arch, triplet = TARGETS[args.arch]
    for name in ('usr/sbin/ember-update', 'usr/libexec/ember-install-slot', 'usr/sbin/pkg_add', 'usr/sbin/pkg_admin'):
        check_program(root, name, arch)
    rc = (root / 'etc/rc.d/ember_update').resolve(strict=True)
    if not rc.is_relative_to(root) or not rc.is_file() or not os.access(rc, os.X_OK):
        raise ValueError('missing executable update service')
    check_anchor(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(mode=0o700)
    try:
        with tempfile.TemporaryDirectory(dir=output.parent) as work:
            layer = Path(work) / 'stage'
            etc = layer / 'etc'
            etc.mkdir(parents=True)
            profile = dict(storage='single', adapter='none', profile='packages-' + arch,
                           target=dict(arch=arch, triplet=triplet, abi='netbsd-11', runtime='libc++'))
            (etc / 'ember-update-profile.json').write_text(json.dumps(profile, indent=2) + '\n')
            source = (root / 'etc/rc.conf').resolve(strict=True)
            if not source.is_relative_to(root):
                raise ValueError('rc.conf is outside the selected root')
            (etc / 'rc.conf').write_bytes(image_policy(source.read_bytes()))
            shutil.copymode(source, etc / 'rc.conf')
            layer.replace(output)
    except BaseException:
        output.rmdir()
        raise


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.exit(f'package-updates: {exc}')
