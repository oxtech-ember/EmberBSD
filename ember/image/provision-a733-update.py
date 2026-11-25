#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Enable signed OS updates on a two-slot A733 Zero 3W image."""

import argparse
import fcntl
import hashlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


SRC = Path(__file__).resolve().parents[2]
PROFILE = {
    'profile': 'a733-zero3w-ember64-minimal-noldap-comp',
    'model': 'xunlong,orangepi-zero3w', 'kernel': '(EMBER64)', 'watchdog': 'sunxiwdt0',
    'module_directory': 'stand/evbarm/11.1/modules',
    'modules': ['bcm2712btcom', 'bcmexpgpio', 'rp1wmcodec', 'rpi5button'],
    'tree_directory': 'dtb/allwinner',
    'trees': ['sun60i-a733-orangepi-zero3w.dtb', 'sun60i-a733-orangepi-zero4.dtb'],
    'sets': ['boot', 'base', 'etc', 'comp'],
}
CONTROL = {'profile': PROFILE['profile'], 'backend': '/usr/libexec/ember-os-slot',
           'health': '/etc/ember-os-health'}
SDK = Path('usr/local/libexec/ember-sdk.pyz')
STATE = Path('var/db/ember-os-update')
SERVICE = Path('etc/rc.conf.d/ember_os_update')
SERVICE_PID = Path('/var/run/ember_os_update.pid')


def check(test, message):
    if not test:
        raise ValueError(message)


def encoded(value):
    return json.dumps(value, sort_keys=True, indent=2).encode() + b'\n'


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic(path, data, mode, *, fat=False, owner=None):
    path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    check(not path.is_symlink() and (not path.exists() or path.is_file()), f'unexpected entry: {path}')
    if path.exists() and path.read_bytes() == data and (fat or stat.S_IMODE(path.stat().st_mode) == mode):
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            if owner is not None:
                os.fchown(stream.fileno(), *owner)
            if not fat:
                os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.replace(temp, path)
            sync_directory(path.parent)
        finally:
            temp.unlink(missing_ok=True)


def check_runtime(root):
    client = root / SDK
    for filename in (client, root / 'usr/pkg/bin/python3.15', root / 'usr/local/etc/ember-sdk.json'):
        check(filename.is_file(), f'native package update layer is missing {filename}')
    with (root / 'usr/pkg/bin/python3.15').open('rb') as file:
        head = file.read(20)
    check(head[:6] == b'\x7fELF\x02\x01' and head[18:20] == b'\xb7\x00',
          'the native package layer needs an AArch64 Python interpreter')
    repos = json.loads((root / 'usr/local/etc/ember-sdk.json').read_bytes())['repositories'].values()
    native = [item['target'] for item in repos if item.get('target') and item.get('install_mode') == 'native']
    check(native == [dict(arch='aarch64', triplet='aarch64--netbsd', abi='netbsd-11', runtime='libc++')],
          'expected exactly the native AArch64 package target')
    result = subprocess.run([sys.executable, '-I', client, '--help'], check=True, capture_output=True)
    check(b'fetch-os-update' in result.stdout, 'install a native SDK client with fetch-os-update')


def load_backend():
    loader = importlib.machinery.SourceFileLoader('slot', str(SRC / 'ember/image/uboot-ffs-slot'))
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader('slot', loader))
    loader.exec_module(module)
    return module


def boot_script(tool, source, timestamp):
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / 'script'
        subprocess.run([tool, '-A', 'arm64', '-C', 'none', '-O', 'netbsd', '-T', 'script',
                        '-a', '0', '-n', 'EmberBSD', '-t', str(timestamp or 1),
                        SRC / 'ember/boot' / source, output], check=True)
        return output.read_bytes()


def check_installed_backend(path):
    meta = path.lstat()
    check(stat.S_ISREG(meta.st_mode) and (meta.st_uid, meta.st_gid) == (0, 0) and not meta.st_mode & 0o022,
          'unsafe installed backend owner, group, mode or type')


def repair_loader(args):
    check(os.geteuid() == 0 and sys.platform.startswith('netbsd'), 'loader repair requires root on EmberBSD')
    backend = load_backend()
    boot = backend.BOOT
    profile = backend.read_profile()
    check(profile['profile'] == PROFILE['profile'], 'unexpected installed profile')
    with SERVICE_PID.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        status, _ = backend.layout(profile)
        check(status['running'] == status['confirmed'] == 'A' and not status['trial'] and
              (boot / backend.NEXT).read_bytes() == b'-', 'confirmed slot A with no pending boot is required')
        entry = boot / 'boot.scr.uimg'
        previous = args.previous_loader.read_bytes()
        installed = Path(CONTROL['backend'])
        check_installed_backend(installed)
        check(not args.previous_backend.samefile(installed), 'supply a separate trusted copy of the previous backend')
        updated_backend = (SRC / 'ember/image/uboot-ffs-slot').read_bytes()
        check(installed.read_bytes() in (args.previous_backend.read_bytes(), updated_backend),
              'installed backend differs from its trusted previous copy')
        board = (boot / 'slotboot.scr').read_bytes()
        check(len(board) >= 64, 'invalid installed slot boot script')
        timestamp = int.from_bytes(board[8:12], 'big')
        check(board == boot_script(args.mkubootimage, 'orangepi-zero4-boot.cmd', timestamp),
              'installed slotboot.scr differs from the selected board script')
        recovery = boot / 'boot.scr'
        check(recovery.is_file() and not recovery.is_symlink() and not args.recovery_loader.samefile(recovery)
              and recovery.read_bytes() == args.recovery_loader.read_bytes(),
              'recovery boot.scr must match its separate trusted slot A copy')
        updated = boot_script(args.mkubootimage, 'uboot-ffs-slots.cmd', timestamp)
        # msdosfs can remove the old FAT entry before it installs the new name.
        # The separately verified boot.scr starts A when the selector is absent.
        check(not entry.is_symlink(), 'unexpected selector link')
        if entry.exists():
            check(entry.is_file() and not args.previous_loader.samefile(entry),
                  'supply a separate trusted copy of the previous selector')
            check(entry.read_bytes() in (previous, updated), 'installed selector differs from its trusted previous copy')
        # Either loader can use the repaired backend. The reverse order could arm
        # a functional trial with an incomplete filesystem after an interruption.
        atomic(installed, updated_backend, 0o555, owner=(0, 0))
        check_installed_backend(installed)
        check(installed.read_bytes() == updated_backend, 'installed backend readback failed')
        atomic(entry, updated, 0o644, fat=True)
        check(entry.read_bytes() == updated, 'installed selector readback failed')


def provision(args):
    live = args.live
    root = Path('/') if live else args.image_root.resolve(strict=True)
    stage = root if live else args.package_layer.resolve(strict=True)
    check(live or not (root == Path('/') or stage == root), 'select a fresh image root and a separate package layer')
    check(bool(re.fullmatch('[0-9a-f]{40}', args.os_commit)), 'invalid OS commit')
    original = args.etc_archive.read_bytes()
    check(hashlib.sha256(original).hexdigest() == args.etc_sha256, 'initial etc archive SHA-256 mismatch')
    head = dict(profile=PROFILE['profile'], generation=0, os_commit=args.os_commit)
    destination = root / STATE / 'release.json'
    check(not destination.exists() or json.loads(destination.read_bytes()) == head,
          'another OS generation is already installed')
    check(not (root / STATE / 'attempt.json').exists() and not (root / 'boot/osplan.dat').exists(),
          'an update has already used this installation')
    check_runtime(stage)
    backend = load_backend()
    if live:
        check(os.geteuid() == 0 and sys.platform.startswith('netbsd'), 'live provisioning requires root on EmberBSD')
        status, _ = backend.layout(PROFILE, provisioning=True)
        check(status['running'] == 'A' and status['confirmed'] in ('A', None) and not status['trial'] and
              status['next'] is None and status['prepared'] is None, 'expected the initial boot of slot A')
        actual = subprocess.check_output(['/sbin/sysctl', '-n', 'kern.buildinfo'], text=True).strip()
        check(actual == 'EmberBSD:' + args.os_commit, 'unexpected running OS commit')
    for path in ('netbsd', 'boot/netbsd.img'):
        check(backend.contains_build(root / path, args.os_commit, PROFILE['kernel']), f'{path} differs from the selected source revision')
    check((root / 'boot/boot.scr').is_file(), 'the normal recovery script is missing')
    for tree in PROFILE['trees']:
        check((root / 'boot' / PROFILE['tree_directory'] / tree).is_file(), f'missing boot DTB: {tree}')

    contents = {
        Path('usr/libexec/ember-os-update'): (SRC / 'libexec/ember-os-update/ember-os-update').read_bytes(),
        Path('etc/rc.d/ember_os_update'): (SRC / 'etc/rc.d/ember_os_update').read_bytes(),
        Path(CONTROL['backend'][1:]): (SRC / 'ember/image/uboot-ffs-slot').read_bytes(),
        Path(CONTROL['health'][1:]): (args.health or SRC / 'ember/image/a733-update-health').read_bytes(),
    }
    configs = {
        Path('etc/ember-os-update.json'): encoded(CONTROL),
        Path('etc/ember-uboot-slots.json'): encoded(PROFILE),
        Path('etc/rc.conf.d/resize_root'): b'resize_root=NO\n',
        Path('etc/rc.conf.d/resize_gpt'): b'resize_gpt=NO\n',
        STATE / 'etc.tar.xz': original,
        STATE / 'release.json': encoded(head),
    }
    if live:
        for name in ('resize_root', 'resize_gpt'):
            policy = Path('etc/rc.conf.d') / name
            current = root / policy
            if current.is_file() and not current.is_symlink():
                text = current.read_bytes()
                if not text.endswith(configs[policy]):
                    configs[policy] = text.rstrip(b'\n') + b'\n' + configs[policy]
                else:
                    configs[policy] = text
    else:
        for filename in (*contents, *configs, SERVICE):
            check(all(not (stage / parent).is_symlink() for parent in filename.parents) and
                  not (stage / filename).exists() and not (stage / filename).is_symlink(),
                  f'package or site overlay overrides profile-owned path: {filename}')
    for name in PROFILE['modules']:
        path = Path(PROFILE['module_directory']) / name / (name + '.kmod')
        if live:
            check((root / path).is_file(), f'missing installed kernel module: {name}')
        else:
            configs[path] = (args.kernel / 'modules' / (name + '.kmod')).read_bytes()

    markers = {'osgood.dat': b'A', 'osback.dat': b'A', 'osnext.dat': b'-'}
    boot = root / 'boot'
    for name, data in markers.items():
        path = boot / name
        check(not path.exists() or path.read_bytes() == data, f'previously used loader marker: {name}')
    scripts = {}
    for source, name in [('orangepi-zero4-boot.cmd', 'slotboot.scr'), ('uboot-ffs-slots.cmd', 'boot.scr.uimg')]:
        scripts[name] = boot_script(args.mkubootimage, source, args.timestamp)
        check(not (boot / name).exists() or (boot / name).read_bytes() == scripts[name],
              f'found a different loader entry: {name}')
    (root / STATE).mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(root / STATE, 0o700)
    for filename, data in contents.items():
        atomic(root / filename, data, 0o555)
    for filename, data in configs.items():
        atomic(root / filename, data, 0o644)
    # Install both fallback markers and their target before the loader entry.
    for name, data in {**markers, **scripts}.items():
        atomic(boot / name, data, 0o644, fat=True)
    atomic(root / SERVICE, b'ember_os_update=YES\n', 0o644)
    if not live:
        with args.mtree.open('a') as spec:
            for path in [*contents, *configs, SERVICE]:
                for parent in reversed(path.parents):
                    if parent != Path('.'):
                        mode = '0700' if parent == STATE else '0755'
                        print(f'./{parent} type=dir uname=root gname=wheel mode={mode}', file=spec)
                mode = '0555' if path in contents else '0644'
                print(f'./{path} type=file uname=root gname=wheel mode={mode}', file=spec)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--live', action='store_true', help='enable the service on a provisioned slot A')
    mode.add_argument('--repair-loader', action='store_true', help='repair the boot path on confirmed slot A')
    parser.add_argument('--recovery-loader', type=Path, help='trusted slot A boot.scr from the original image or boot set')
    parser.add_argument('--previous-backend', type=Path, help='trusted original uboot-ffs-slot for repair')
    parser.add_argument('--previous-loader', type=Path, help='trusted original boot.scr.uimg for selector repair')
    mode.add_argument('--image-root', type=Path, help='populate a fresh image stage')
    parser.add_argument('--package-layer', type=Path)
    parser.add_argument('--mtree', type=Path)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--os-commit')
    parser.add_argument('--etc-archive', type=Path)
    parser.add_argument('--etc-sha256', help='exact digest from the verified release')
    parser.add_argument('--mkubootimage', type=Path, required=True)
    parser.add_argument('--health', type=Path, help='install this application health check')
    parser.add_argument('--timestamp', type=int, default=0)
    args = parser.parse_args()
    stage = (args.package_layer, args.mtree, args.kernel)
    baseline = (args.os_commit, args.etc_archive, args.etc_sha256)
    originals = (args.previous_loader, args.previous_backend, args.recovery_loader)
    if args.repair_loader:
        if not all(originals) or any((*stage, *baseline, args.health, args.timestamp)):
            parser.error('--repair-loader takes --previous-loader, --previous-backend, --recovery-loader and --mkubootimage')
    else:
        if any(originals) or not all(baseline):
            parser.error('initial installation requires --os-commit, --etc-archive and --etc-sha256')
        if args.image_root and not all(stage):
            parser.error('--image-root requires --package-layer, --kernel and --mtree')
        if args.live and any(stage):
            parser.error('image staging arguments require --image-root')
    try:
        if args.repair_loader:
            repair_loader(args)
        else:
            provision(args)
    except (KeyError, ValueError, OSError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'Provisioning stopped: {exc}\n')


if __name__ == '__main__':
    main()
