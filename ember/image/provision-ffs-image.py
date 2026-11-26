#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build a fresh FFS update image from a native release and a hardware profile."""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

SRC = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from os_update_image import ANCHOR, TARGET_ELF, atomic, check, check_anchor, check_program, encoded, elf_abi, image_policy

DB = Path('var/db/ember-update')
PROFILE = Path('etc/ember-update-profile.json')
NATIVE = ('usr/sbin/ember-update', 'usr/libexec/ember-os-slot', 'usr/libexec/ember-install-slot')
SCRIPTS = ('etc/rc.d/ember_update', 'usr/libexec/ember-stage-ffs')


def word(value, pattern):
    check(isinstance(value, str) and re.fullmatch(pattern, value), f'invalid profile value: {value!r}')


def unique_fields(pairs):
    value = dict(pairs)
    check(len(value) == len(pairs), 'duplicate OS descriptor field')
    return value


def installed_release(args, profile):
    if args.release is None and args.release_sha256 is None:
        return encoded(dict(profile=profile['profile'], generation=0, os_commit=args.os_commit))
    check(args.release and args.release_sha256, 'supply the verified descriptor and its release SHA-256 together')
    with args.release.open('rb') as stream:
        data = stream.read(32769)
    check(len(data) <= 32768 and hashlib.sha256(data).hexdigest() == args.release_sha256,
          'OS descriptor size or SHA-256 mismatch')
    head = json.loads(data.decode('utf-8'), object_pairs_hook=unique_fields)
    check(isinstance(head, dict) and set(head) == {'schema', 'profile', 'os_commit', 'generation', 'target', 'sets'}
          and head['schema'] == 'emberbsd.os-update.v1' and head['profile'] == profile['profile']
          and head['target'] == profile['target'] and head['os_commit'] == args.os_commit,
          'OS descriptor differs from the image profile or kernel')
    check(type(head['generation']) is int and 1 <= head['generation'] < 2**63, 'invalid OS generation')
    sets = head['sets']
    check(isinstance(sets, dict) and set(sets) == set(profile['sets']), 'OS descriptor differs from image sets')
    for artifact in sets.values():
        word(artifact, r'[a-z0-9][a-z0-9._-]*')
    check(len(set(sets.values())) == len(sets), 'OS descriptor repeats a set')
    return data


def validate(description, board, adapter, sets=None):
    p = description['slots']
    check(description['image'] == board, 'release profile does not match the image board')
    check(p['adapter'] == adapter and p['storage'] == 'ffs', 'profile differs from the boot adapter')
    check(set(p['roots']) == {'A', 'B'}, 'two named root slots required')
    labels = [p['roots']['A'], p['roots']['B'], p['esp']]
    for label in labels:
        word(label, r'[A-Za-z0-9][A-Za-z0-9._-]{0,35}')
    check(len(set(labels)) == 3, 'duplicate GPT labels')
    word(p['profile'], r'[a-z0-9][a-z0-9._-]*')
    word(p['kernel'], r'\([A-Za-z0-9_]+\)')
    word(p['watchdog'], r'[a-z][a-z0-9]*')
    word(p['module_directory'], r'stand/[A-Za-z0-9_-]+/[0-9.]+/modules')
    check(isinstance(p['model'], str) and p['model'].strip(), 'missing hardware model')
    for k in ('arch', 'abi', 'runtime', 'triplet'):
        word(p['target'][k], r'[A-Za-z0-9_+.-]+')
    for k, pattern in [('trees', r'[A-Za-z0-9_-]+\.dtb'), ('modules', r'[A-Za-z0-9_]+')]:
        check(isinstance(p[k], list) and len(set(p[k])) == len(p[k]), f'invalid {k}')
        for value in p[k]:
            word(value, pattern)
    selected = set(p['sets'])
    check(len(selected) == len(p['sets']) and selected in ({'base', 'boot', 'etc'}, {'base', 'boot', 'etc', 'comp'}),
          'unsupported OS set selection')
    if sets is not None:
        check(set(sets) == selected - {'boot'}, 'release profile differs from the selected distribution sets')
    if adapter == 'uboot-ffs':
        check(p['target']['arch'] == 'aarch64' and description['uboot_arch'] == 'arm64',
              'raw ARM64 boot requires an AArch64 release')
        check(p['trees'], 'missing boot DTBs')
        word(p['tree_directory'], r'dtb(?:/[A-Za-z0-9_-]+)*')
        for key in ('marker_address', 'script_address'):
            word(description[key], r'0x[0-9A-Fa-f]+')
        check(int(description['marker_address'], 16) != int(description['script_address'], 16),
              'overlapping loader buffers')
        check((SRC / description['boot_script']).is_file(), 'missing board boot script')
    elif adapter == 'efiboot':
        word(p['reset_provider'], r'[a-z0-9][a-z0-9-]+')
    else:
        raise ValueError('unsupported boot adapter')
    check((SRC / description['health']).is_file(), 'missing health check')
    return p


def verified_kernel(path, commit, tag, raw=False):
    with path.open('rb') as fd, mmap.mmap(fd.fileno(), 0, access=mmap.ACCESS_READ) as data:
        expected = data[56:60] == b'ARMd' if raw else data[:4] == b'\x7fELF'
        check(expected and data.find(tag.encode()) >= 0 and
              re.search(b'EmberBSD:' + commit.encode() + rb'\n?\x00', data), f'incompatible kernel: {path}')


def build_script(tool, source, path, timestamp, description):
    with tempfile.TemporaryDirectory() as work:
        src, out = Path(work) / 'script.cmd', Path(work) / 'script'
        src.write_text(source)
        subprocess.run([tool, '-A', description['uboot_arch'], '-C', 'none', '-O', 'netbsd', '-T', 'script',
                        '-a', '0', '-n', 'EmberBSD', '-t', str(timestamp or 1), src, out], check=True)
        atomic(path, out.read_bytes(), 0o644, fat=True)


def uboot(description, args, root):
    p, boot = description['slots'], root / 'boot'
    verified_kernel(boot / 'netbsd.img', args.os_commit, p['kernel'], True)
    check((boot / 'boot.scr').is_file(), 'missing default recovery boot')
    for tree in p['trees']:
        check((boot / p['tree_directory'] / tree).is_file(), f'missing DTB: {tree}')
    selector = (SRC / 'ember/boot/uboot-ffs-slots.cmd').read_text()
    for token, value in [('MARKER_ADDRESS', description['marker_address']), ('SCRIPT_ADDRESS', description['script_address']),
                         ('ROOT_A', p['roots']['A']), ('ROOT_B', p['roots']['B'])]:
        selector = selector.replace('@' + token + '@', value)
    build_script(args.mkubootimage, (SRC / description['boot_script']).read_text(), boot / 'slotboot.scr',
                 args.timestamp, description)
    build_script(args.mkubootimage, selector, boot / 'boot.scr.uimg', args.timestamp, description)
    return boot


def provision(args, description, p):
    root, base = args.image_root, args.base_root
    check(root.resolve() != Path('/') and base.resolve() != root.resolve(), 'select a new image stage')
    arch = p['target']['arch']
    check(elf_abi(root / 'netbsd') == TARGET_ELF.get(arch),
          'kernel ELF differs from the hardware profile target')
    for rel in NATIVE:
        check_program(base, rel, arch)
    for rel in SCRIPTS:
        path = (base / rel).resolve(strict=True)
        check(path.is_relative_to(base.resolve()) and path.is_file() and os.access(path, os.X_OK), f'missing base program: {rel}')
    check_anchor(base)
    word(args.os_commit, r'[0-9a-f]{40}')
    verified_kernel(root / 'netbsd', args.os_commit, p['kernel'])
    archive = args.etc_archive.read_bytes()
    check(hashlib.sha256(archive).hexdigest() == args.etc_sha256, 'initial etc archive SHA-256 mismatch')
    files = {PROFILE: (encoded(p), 0o644), DB / 'os-release.json': (installed_release(args, p), 0o600),
             DB / 'etc.tar.xz': (archive, 0o600),
             Path('etc/ember-os-health'): ((args.health or SRC / description['health']).read_bytes(), 0o555),
             Path('etc/rc.conf.d/resize_root'): (b'resize_root=NO\n', 0o644),
             Path('etc/rc.conf.d/resize_gpt'): (b'resize_gpt=NO\n', 0o644)}
    for name in p['modules']:
        files[Path(p['module_directory']) / name / (name + '.kmod')] = (
            (args.kernel / 'modules' / (name + '.kmod')).read_bytes(), 0o444)
    if args.overlay:
        for rel in [*files, *map(Path, NATIVE + SCRIPTS), ANCHOR, Path('etc/fstab'), Path('netbsd'), Path('boot')]:
            check(all(not (args.overlay / parent).is_symlink() for parent in rel.parents) and
                  not os.path.lexists(args.overlay / rel), f'overlay overrides profile-owned path: {rel}')
    if args.adapter == 'uboot-ffs':
        markers = uboot(description, args, root)
    else:
        from efiboot_slots import initialize_efi
        loader = description['efi_loader']
        check(Path(loader).name == loader, 'invalid EFI loader file name')
        markers = initialize_efi(root / 'boot', args.kernel / loader, p, indices={'A': 2, 'B': 3})
    for name, value in {'osgood.dat': b'A', 'osback.dat': b'A', 'osnext.dat': b'-'}.items():
        atomic(markers / name, value, 0o644, fat=True)
    with args.mtree.open('a') as spec:
        for rel, (data, mode) in files.items():
            atomic(root / rel, data, mode)
            for parent in reversed(rel.parents):
                if parent != Path('.'):
                    print(f'./{parent} type=dir uname=root gname=wheel mode=0755', file=spec)
            print(f'./{rel} type=file uname=root gname=wheel mode={mode:04o}', file=spec)
    # The site's rc.conf can come from the final overlay. Preserve its bytes and mode.
    rc = Path('etc/rc.conf')
    source = args.overlay / rc if args.overlay and (args.overlay / rc).is_file() else root / rc
    check(not source.is_symlink(), 'unexpected rc.conf link')
    data = source.read_bytes()
    atomic(args.policy_layer / rc, image_policy(data), stat.S_IMODE(source.stat().st_mode))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', required=True)
    parser.add_argument('--adapter', required=True)
    parser.add_argument('--profile', required=True, type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check-image', action='store_true')
    mode.add_argument('--image-root', type=Path)
    parser.add_argument('--sets', nargs='+')
    for opt in ('kernel', 'base-root', 'overlay', 'policy-layer', 'etc-archive', 'mtree', 'health'):
        parser.add_argument('--' + opt, type=Path)
    parser.add_argument('--os-commit')
    parser.add_argument('--release', type=Path)
    parser.add_argument('--release-sha256')
    parser.add_argument('--etc-sha256')
    parser.add_argument('--timestamp', type=int, default=0)
    parser.add_argument('--mkubootimage', default=os.environ.get('TOOL_MKUBOOTIMAGE', 'mkubootimage'))
    args = parser.parse_args()
    try:
        description = json.loads(args.profile.read_bytes())
        p = validate(description, args.board, args.adapter, args.sets)
        if args.check_image:
            print(p['roots']['A'], p['roots']['B'], p['esp'])
            print(' '.join(p['trees']))
        else:
            provision(args, description, p)
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'Cannot provision image: {exc}\n')


if __name__ == '__main__':
    main()
