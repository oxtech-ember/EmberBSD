#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Provision the GPT, U-Boot and FFS boot adapter for a signed OS release profile."""

import argparse
from contextlib import contextmanager, nullcontext
import fcntl
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
# Isolated Python on the device loads the module from this OS source tree.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from os_update_image import (BACKEND, SERVICE, STATE, atomic, check, check_runtime, elf_abi, encoded,
                             service_files, sync_directory)

SERVICE_PID = Path('/var/run/ember_os_update.pid')


def read_image_profile(path):
    image = json.loads(path.read_bytes())
    for key, pattern in [('image', r'[a-zA-Z0-9_-]+'),
                         ('uboot_arch', r'[a-zA-Z0-9_]+'),
                         ('marker_address', r'0x[0-9a-fA-F]+'), ('script_address', r'0x[0-9a-fA-F]+')]:
        check(isinstance(image[key], str) and re.fullmatch(pattern, image[key]), f'invalid image profile {key}')
    check(int(image['marker_address'], 16) != int(image['script_address'], 16), 'U-Boot buffers overlap')
    for key in ('health', 'boot_script'):
        value = image[key]
        check(isinstance(value, str) and value and '..' not in Path(value).parts
              and (SRC / value).is_file(), f'missing or invalid {key} path')
    target = image['slots']['target']
    check((target['arch'], image['uboot_arch']) == ('aarch64', 'arm64'),
          'this U-Boot boot adapter requires an ARM64 raw kernel image')
    load_backend().validate_profile(image['slots'])
    return image




def load_backend():
    loader = importlib.machinery.SourceFileLoader('slot', str(SRC / 'ember/image/uboot-ffs-slot'))
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader('slot', loader))
    loader.exec_module(module)
    return module


def boot_script(tool, source, timestamp, image):
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / 'script'
        script = SRC / source
        if source == 'ember/boot/uboot-ffs-slots.cmd':
            selector = script.read_text().replace('@MARKER_ADDRESS@', image['marker_address'])
            script = Path(temporary) / 'selector.cmd'
            for slot in ('A', 'B'):
                selector = selector.replace(f'@ROOT_{slot}@', image['slots']['roots'][slot])
            script.write_text(selector.replace('@SCRIPT_ADDRESS@', image['script_address']))
        subprocess.run([tool, '-A', image['uboot_arch'], '-C', 'none', '-O', 'netbsd', '-T', 'script',
                        '-a', '0', '-n', 'EmberBSD', '-t', str(timestamp or 1),
                        script, output], check=True)
        return output.read_bytes()


def check_installed_backend(path):
    meta = path.lstat()
    check(stat.S_ISREG(meta.st_mode) and (meta.st_uid, meta.st_gid) == (0, 0) and not meta.st_mode & 0o022,
          'unsafe installed backend owner, group, mode or type')


@contextmanager
def service_lock():
    check(os.geteuid() == 0 and sys.platform.startswith('netbsd'), 'live maintenance requires root on EmberBSD')
    with SERVICE_PID.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def repair_loader(args):
    backend = load_backend()
    boot = backend.BOOT
    with service_lock():
        profile = args.image['slots']
        old_config = backend.PROFILE.with_name('ember-uboot-slots.json')
        if old_config.exists() or old_config.is_symlink():
            check_installed_backend(old_config)
            # The first released adapter used this fixed GPT layout.
            old = {k: v for k, v in profile.items() if k not in ('adapter', 'storage', 'roots', 'esp', 'target')}
            check(profile['roots'] == {'A': 'netbsd-root', 'B': 'ember-root-b'} and profile['esp'] == 'EFI'
                  and json.loads(old_config.read_bytes()) == old, 'previous hardware profile differs')
        if backend.PROFILE.exists() or backend.PROFILE.is_symlink():
            check(backend.read_profile() == profile, 'unexpected installed profile')
        else:
            check(old_config.exists(), 'missing installed boot profile')
        status, _ = backend.layout(profile)
        check(status['running'] == status['confirmed'] == 'A' and not status['trial'] and
              (boot / backend.NEXT).read_bytes() == b'-', 'confirmed slot A with no pending boot is required')
        entry = boot / 'boot.scr.uimg'
        previous = args.previous_loader.read_bytes()
        installed = Path('/') / BACKEND
        check_installed_backend(installed)
        check(not args.previous_backend.samefile(installed), 'supply a separate trusted copy of the previous backend')
        updated_backend = (SRC / 'ember/image/uboot-ffs-slot').read_bytes()
        check(installed.read_bytes() in (args.previous_backend.read_bytes(), updated_backend),
              'installed backend differs from its trusted previous copy')
        board_path = boot / 'slotboot.scr'
        check(not board_path.is_symlink(), 'unexpected board script link')
        board = board_path.read_bytes() if board_path.is_file() else None
        baseline = args.previous_board.read_bytes() if args.previous_board else board
        check(baseline is not None and len(baseline) >= 64, 'supply the trusted previous board script')
        timestamp = int.from_bytes(baseline[8:12], 'big')
        new_board = boot_script(args.mkubootimage, args.image['boot_script'], timestamp, args.image)
        check(board is None or board == new_board or (args.previous_board and
              not args.previous_board.samefile(board_path) and board == baseline),
              'installed slotboot.scr differs from the trusted board script')
        recovery = boot / 'boot.scr'
        check(recovery.is_file() and not recovery.is_symlink() and not args.recovery_loader.samefile(recovery)
              and recovery.read_bytes() == args.recovery_loader.read_bytes(),
              'recovery boot.scr must match its separate trusted slot A copy')
        updated = boot_script(args.mkubootimage, 'ember/boot/uboot-ffs-slots.cmd', timestamp, args.image)
        # msdosfs can remove the old FAT entry before it installs the new name.
        # The separately verified boot.scr starts A when the selector is absent.
        check(not entry.is_symlink(), 'unexpected selector link')
        if entry.exists():
            check(entry.is_file() and not args.previous_loader.samefile(entry),
                  'supply a separate trusted copy of the previous selector')
            check(entry.read_bytes() in (previous, updated), 'installed selector differs from its trusted previous copy')
        # Either loader can use the repaired backend. The reverse order could arm
        # a functional trial with an incomplete filesystem after an interruption.
        helper = installed.with_name('ember_ffs_slot.py')
        if helper.exists() or helper.is_symlink():
            check_installed_backend(helper)
        atomic(helper, (SRC / 'ember/image/ember_ffs_slot.py').read_bytes(), 0o444, owner=(0, 0))
        atomic(backend.PROFILE, encoded(profile), 0o644, owner=(0, 0))
        atomic(installed, updated_backend, 0o555, owner=(0, 0))
        check_installed_backend(installed)
        check(installed.read_bytes() == updated_backend, 'installed backend readback failed')
        atomic(entry, updated, 0o644, fat=True)
        check(entry.read_bytes() == updated, 'installed selector readback failed')
        # The new selector can launch the previous board script. Switch its input first.
        atomic(boot / 'slotboot.scr', new_board, 0o644, fat=True)
        check((boot / 'slotboot.scr').read_bytes() == new_board, 'board script readback failed')
        if old_config.exists():
            old_config.unlink()
            sync_directory(old_config.parent)



def provision(args):
    with service_lock() if args.live else nullcontext():
        provision_root(args)


def provision_root(args):
    live = args.live
    root = Path('/') if live else args.image_root.resolve(strict=True)
    stage = root if live else args.package_layer.resolve(strict=True)
    check(live or not (root == Path('/') or stage == root), 'select a fresh image root and a separate package layer')
    profile = args.image['slots']
    contents, configs = service_files(root, profile['profile'], args.os_commit,
                                      args.etc_archive, args.etc_sha256, SRC / 'ember/image/uboot-ffs-slot',
                                      args.health or SRC / args.image['health'])
    check(not (root / 'boot/osplan.dat').exists(), 'an update has already used this installation')
    check_runtime(stage, profile['target'], root / 'netbsd')
    check(elf_abi(root / 'netbsd') == (b'\x02\x01', b'\xb7\0'), 'expected an AArch64 kernel ELF')
    backend = load_backend()
    if live:
        status, _ = backend.layout(profile, provisioning=True)
        check(status['running'] == 'A' and status['confirmed'] in ('A', None) and not status['trial'] and
              status['next'] is None and status['prepared'] is None, 'expected the initial boot of slot A')
        actual = subprocess.check_output(['/sbin/sysctl', '-n', 'kern.buildinfo'], text=True).strip()
        check(actual == 'EmberBSD:' + args.os_commit, 'unexpected running OS commit')
    for path in ('netbsd', 'boot/netbsd.img'):
        check(backend.contains_build(root / path, args.os_commit, profile['kernel']), f'{path} differs from the selected source revision')
    check((root / 'boot/boot.scr').is_file(), 'the normal recovery script is missing')
    for tree in profile['trees']:
        check((root / 'boot' / profile['tree_directory'] / tree).is_file(), f'missing boot DTB: {tree}')

    configs.update({
        BACKEND.with_name('ember_ffs_slot.py'): (SRC / 'ember/image/ember_ffs_slot.py').read_bytes(),
        Path('etc/ember-update-profile.json'): encoded(profile),
        Path('etc/rc.conf.d/resize_root'): b'resize_root=NO\n',
        Path('etc/rc.conf.d/resize_gpt'): b'resize_gpt=NO\n',
    })
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
    for name in profile['modules']:
        path = Path(profile['module_directory']) / name / (name + '.kmod')
        if live:
            check((root / path).is_file(), f'missing installed kernel module: {name}')
        else:
            configs[path] = (args.kernel / 'modules' / (name + '.kmod')).read_bytes()

    if not live:
        for filename in (*contents, *configs, SERVICE, Path('boot'), Path('netbsd')):
            check(all(not (stage / parent).is_symlink() for parent in filename.parents) and
                  not (stage / filename).exists() and not (stage / filename).is_symlink(),
                  f'package or site overlay overrides profile-owned path: {filename}')

    markers = {'osgood.dat': b'A', 'osback.dat': b'A', 'osnext.dat': b'-'}
    boot = root / 'boot'
    for name, data in markers.items():
        path = boot / name
        check(not path.exists() or path.read_bytes() == data, f'previously used loader marker: {name}')
    scripts = {}
    for source, name in [(args.image['boot_script'], 'slotboot.scr'), ('ember/boot/uboot-ffs-slots.cmd', 'boot.scr.uimg')]:
        scripts[name] = boot_script(args.mkubootimage, source, args.timestamp, args.image)
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
    mode.add_argument('--check-image', metavar='BOARD', help='validate the image inputs and print the required DTBs')
    parser.add_argument('--profile', required=True, type=Path, help='image profile JSON')
    parser.add_argument('--sets', nargs='+', help='distribution sets selected by the image builder')
    mode.add_argument('--live', action='store_true', help='enable the service on a provisioned slot A')
    mode.add_argument('--repair-loader', action='store_true', help='repair the boot path on confirmed slot A')
    parser.add_argument('--recovery-loader', type=Path, help='trusted slot A boot.scr from the original image or boot set')
    parser.add_argument('--previous-backend', type=Path, help='trusted original uboot-ffs-slot for repair')
    parser.add_argument('--previous-board', type=Path, help='trusted previous slotboot.scr if the board script changes')
    parser.add_argument('--previous-loader', type=Path, help='trusted original boot.scr.uimg for selector repair')
    mode.add_argument('--image-root', type=Path, help='populate a fresh image stage')
    parser.add_argument('--package-layer', type=Path)
    parser.add_argument('--mtree', type=Path)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--os-commit')
    parser.add_argument('--etc-archive', type=Path)
    parser.add_argument('--etc-sha256', help='exact digest from the verified release')
    parser.add_argument('--mkubootimage', type=Path, default=os.environ.get('TOOL_MKUBOOTIMAGE', 'mkubootimage'))
    parser.add_argument('--health', type=Path, help='install this application health check')
    parser.add_argument('--timestamp', type=int, default=0)
    args = parser.parse_args()
    try:
        args.image = read_image_profile(args.profile)
        if args.check_image:
            check(args.image['image'] == args.check_image, 'release profile does not match the image board')
            check(set(args.sets or ()) == set(args.image['slots']['sets']) - {'boot'},
                  'release profile differs from the selected distribution sets')
            slots = args.image['slots']
            print(slots['roots']['A'], slots['roots']['B'], slots['esp'])
            print(' '.join(slots['trees']))
            return
    except (KeyError, TypeError, ValueError, OSError) as exc:
        parser.exit(1, f'Invalid image profile: {exc}\n')
    if args.sets:
        parser.error('--sets belongs to --check-image')
    stage = (args.package_layer, args.mtree, args.kernel)
    baseline = (args.os_commit, args.etc_archive, args.etc_sha256)
    originals = (args.previous_loader, args.previous_backend, args.recovery_loader)
    if args.repair_loader:
        if not all(originals) or any((*stage, *baseline, args.health, args.timestamp)):
            parser.error('--repair-loader takes --previous-loader, --previous-backend, --recovery-loader and --mkubootimage')
    else:
        if args.previous_board or any(originals) or not all(baseline):
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
