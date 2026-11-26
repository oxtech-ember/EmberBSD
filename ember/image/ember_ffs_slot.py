# SPDX-License-Identifier: BSD-2-Clause
"""Prepare an inactive FFS slot. The boot adapter validates its device and installs its boot files."""

from contextlib import ExitStack
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


STATE = Path('var/db/ember-os-update')
TAR = '/bin/tar'


def check(ok, message):
    if not ok:
        raise ValueError(message)


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def digest(stream):
    return hashlib.file_digest(stream, 'sha256').digest()


def member_id(item, archive):
    if item.isfile() or item.islnk():
        with archive.extractfile(item) as stream:
            return ('file', digest(stream), item.mode & 0o7777, item.uid, item.gid)
    if item.isdir():
        return ('dir', item.mode & 0o7777, item.uid, item.gid)
    if item.issym():
        return ('link', item.linkname, item.uid, item.gid)
    return None


def defaults(archive, linked=None):
    with tarfile.open(archive, 'r:xz') as tar:
        result = {}
        for item in tar:
            path = Path(item.name)
            check(not path.is_absolute() and '..' not in path.parts, 'invalid defaults path')
            if path == Path('.'):
                continue
            if linked is not None and item.islnk():
                target = Path(item.linkname)
                linked[path] = linked.get(target, target)
            identity = member_id(item, tar)
            if identity:
                result[path] = identity
        return result


def installed_id(root, relative):
    path = root / relative
    check(path.parent.resolve().is_relative_to(root), f'configuration parent escapes slot: {relative}')
    if path.is_symlink():
        return ('link', os.readlink(path), path.lstat().st_uid, path.lstat().st_gid)
    if path.is_file():
        with path.open('rb') as stream:
            info = os.fstat(stream.fileno())
            return ('file', digest(stream), info.st_mode & 0o7777, info.st_uid, info.st_gid)
    if path.is_dir():
        info = path.stat()
        return ('dir', info.st_mode & 0o7777, info.st_uid, info.st_gid)
    return None if not path.exists() else ('other',)


def keeps_private_mode(old, new, local):
    """A site can restrict group or public reads independently of a content update."""
    if not (old and new and local and old[0] == new[0] == local[0] == 'file'):
        return False
    if old[2:] != new[2:] or local[3:] != old[3:]:
        return False
    mode, site = old[2], local[2]
    return site != mode and (site | mode) == mode and not ((mode ^ site) & ~0o044)


def merge_defaults(root, old_archive, new_archive):
    linked = {}
    before, after = defaults(old_archive), defaults(new_archive, linked)
    changed_groups = {target for alias, target in linked.items()
                      if before.get(alias) != after[alias] or before.get(target) != after[target]}
    guarded = changed_groups | {alias for alias, target in linked.items() if target in changed_groups}
    install, removed_dirs, private_modes = [], [], {}
    for path in sorted(before.keys() | after.keys()):
        old, new = before.get(path), after.get(path)
        if old == new and path not in guarded:
            continue
        actual = installed_id(root, path)
        if actual == new:
            continue
        dest = root / path
        if path in guarded:
            check(actual == old, f'local change conflicts with a release hard link: {path}')
        elif keeps_private_mode(old, new, actual):
            if actual[1] == new[1]:
                continue
            check(actual[1] == old[1], f'local and release changes overlap: {path}')
            private_modes[dest] = actual[2]
        else:
            check(actual == old, f'local and release changes overlap: {path}')
        if new is None and old[0] == 'dir':
            removed_dirs.append(dest)
        elif new is None:
            dest.unlink()
        elif new[0] == 'dir':
            check(old is None or old[0] == 'dir', f'default changes filesystem type: {path}')
            dest.mkdir(parents=True, exist_ok=True)
            os.chown(dest, new[2], new[3])
            dest.chmod(new[1])
        else:
            check(old is None or old[0] != 'dir', f'default replaces a directory: {path}')
            install.append('./' + str(path))
    if install:
        names = b'\0'.join(os.fsencode(name) for name in install) + b'\0'
        run(TAR, '-xpf', new_archive, '-C', root, '--safe-writes', '--null', '-T', '/dev/stdin', input=names)
        for dest, mode in private_modes.items():
            dest.chmod(mode)
    for dest in reversed(removed_dirs):
        if not any(dest.iterdir()):
            dest.rmdir()


def snapshot_copy(root, work, stack):
    source = work / 'snapshot'
    source.mkdir(mode=0o700)
    stack.callback(source.rmdir)
    free = re.search(r'^(fss\d+): not in use$', output('/usr/sbin/fssconfig', '-l'), re.M)
    check(free, 'no free FFS snapshot device')
    device = free[1]
    run('/usr/sbin/fssconfig', '-x', device, '/', str(work / 'backing'))
    stack.callback(run, '/usr/sbin/fssconfig', '-u', device)
    run('/sbin/mount', '-r', '-t', 'ffs', '/dev/' + device, source)
    stack.callback(run, '/sbin/umount', source)
    copy_snapshot_tree(source, root)


def copy_snapshot_tree(source, root):
    # Copy a consistent system, including pkgdb, accounts, DNS, SSH and firmware.
    # Mounted volumes retain their own storage and fstab entries.
    skip = ['boot', 'proc', 'dev/pts', 'tmp', 'var/tmp', 'var/run', 'var/shm',
            'var/cache']
    args = [TAR, '--one-file-system', '-cpf', '-']
    for path in skip:
        # bsdtar also matches the directory itself with a trailing /*.
        args.extend(('--exclude', './' + path + '/?*'))
    # The package lock and OS journal contain process state from the running slot.
    args.extend(('--exclude', './' + str(STATE / 'incoming'),
                 '--exclude', './' + str(STATE / 'attempt.json'),
                 '--exclude', './var/db/emberbsd-sdk/client.lock'))
    with subprocess.Popen([*args, '.'], cwd=source, stdout=subprocess.PIPE) as writer:
        try:
            run(TAR, '-xpf', '-', '-C', root, stdin=writer.stdout)
        finally:
            writer.stdout.close()
        check(writer.wait() == 0, 'cannot copy the source snapshot')


def prepare_root(device, directory, receipt, roots, slot, invalidate, install_boot):
    """Replace a dedicated, inactive wedge. Return after both filesystems are unmounted."""
    check(re.fullmatch(r'dk[0-9]+', device), 'expected an FFS wedge')
    check(not re.search(r'^/dev/' + device + r' on ', output('/sbin/mount'), re.M), 'target is mounted')
    invalidate()
    run('/sbin/newfs', '-O', '1', '/dev/r' + device)
    work = Path(tempfile.mkdtemp(prefix='ember-slot-', dir='/var/run'))
    with ExitStack() as stack:
        stack.callback(work.rmdir)
        root = work / 'target'
        root.mkdir(mode=0o700)
        stack.callback(root.rmdir)
        # A partial target has no boot permission and is reformatted on retry.
        # Flush and unmount it before recording the prepared identity.
        run('/sbin/mount', '-t', 'ffs', '-o', 'async', '/dev/' + device, root)
        stack.callback(run, '/sbin/umount', root)
        snapshot_copy(root, work, stack)
        # The native base supplies programs and libraries. Preserve site files and device firmware.
        run(TAR, '-xpf', directory / 'base.tar.xz', '-C', root, '--safe-writes',
            '--exclude', './etc/*', '--exclude', './var/*', '--exclude', './usr/pkg',
            '--exclude', './usr/local', '--exclude', './libdata/firmware', '--exclude', './usr/libdata/firmware')
        run(TAR, '-xpf', directory / 'base.tar.xz', '-C', root, '--safe-writes',
            './etc/release', './etc/mtree/set.base')
        if 'comp' in receipt['sets']:
            run(TAR, '-xpf', directory / 'comp.tar.xz', '-C', root, '--safe-writes')
        state = root / STATE
        # Some profiles install the updater in an image layer over common sets.
        # This copy retains that layer until a release replaces its programs.
        merge_defaults(root, state / 'etc.tar.xz', directory / 'etc.tar.xz')
        shutil.copyfile(directory / 'etc.tar.xz', state / 'etc.tar.xz')
        fstab = root / 'etc/fstab'
        table = fstab.read_text()
        labels = '|'.join(re.escape(label) for label in roots.values())
        table, count = re.subn(r'(?m)^NAME=(?:' + labels + r')(\s+/\s+ffs\s+)',
                               'NAME=' + roots[slot] + r'\1', table)
        check(count == 1, 'expected one named FFS root in fstab')
        fstab.write_text(table)
        install_boot(root)
        (state / 'attempt.json').unlink(missing_ok=True)
        shutil.copyfile(directory / 'update.json', state / 'release.json')
        run('/usr/sbin/chroot', root, '/bin/sh', '-ec',
            'for dir in /boot /var/run /var/tmp /tmp; do test -d "$dir" || exit 1; done; '
            'while read -r dev dir fs opts rest; do '
            'case ",$opts," in *,noauto,*) continue ;; esac; '
            'case "$dir" in /dev/pts|/proc|/var/shm) test -d "$dir" || exit 1 ;; esac; '
            'done </etc/fstab; '
            'test -x /etc/rc.d/ember_os_update && test -x /usr/libexec/ember-os-update && '
            '/usr/pkg/bin/python3.15 -I -c "import ssl" && '
            '. /etc/rc.subr && load_rc_config resize_root && ! checkyesno resize_root && '
            'load_rc_config resize_gpt && ! checkyesno resize_gpt')
        os.sync()
