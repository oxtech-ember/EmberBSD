# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Shared target-independent component selection for a static console."""

from pathlib import Path


def runtime_original_source(foundation_receipt, explicit=''):
    """Return the selected runtime's recorded original source tree."""
    source = explicit or foundation_receipt.get('paths', {}).get('llvm-source', '')
    return str(source) if source else ''


def network_orchestrator_log(output):
    """Choose a fresh log path without replacing prior incremental-run logs."""
    import os
    import time

    logs = Path(output) / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    while True:
        path = logs / ('orchestrator-network-{}-{}.log'.format(time.time_ns(), os.getpid()))
        if not path.exists():
            return path


def network_output_action(output, compatibility, wrapper_checks, build_checks=()):
    """Accept a fresh output or reuse a profile-owned compatible build tree."""
    import json
    import os

    output = Path(output)
    if output.is_symlink():
        raise ValueError('network output must not be a symlink')
    if not output.exists():
        return 'new'
    if not output.is_dir():
        raise ValueError('network output must be a directory')
    for name in ('dest', 'obj', 'routing', 'logs'):
        item = output / name
        if item.is_symlink() or not item.is_dir():
            raise ValueError('existing network output is not a producer-owned build tree')
    marker = output / 'NETWORK-COMPAT.json'
    if marker.is_symlink():
        raise ValueError('network compatibility record must not be a symlink')
    recorded = None
    legacy_match = False
    if marker.exists():
        if not marker.is_file():
            raise ValueError('network compatibility record is not a regular file')
        try:
            recorded = json.loads(marker.read_text())
        except (OSError, UnicodeError, ValueError) as exc:
            raise ValueError('network compatibility record is invalid') from exc
        if recorded != compatibility:
            raise ValueError('network output target, ABI, runtime, or compile configuration differs')
    wrapper = output / ('nbmake-' + str(compatibility['machine']))
    if wrapper.is_symlink() or not wrapper.is_file() or not os.access(wrapper, os.X_OK):
        raise ValueError('existing network output has no usable target wrapper')
    wrapper_text = wrapper.read_text()
    required_wrapper_checks = tuple(wrapper_checks)
    if recorded is None:
        required_wrapper_checks += tuple(build_checks)
    if any(value not in wrapper_text for value in required_wrapper_checks):
        raise ValueError('existing network output has an incompatible target configuration')
    for name in ('root', 'crunch'):
        item = output / name
        if item.is_symlink() or (item.exists() and not item.is_dir()):
            raise ValueError('existing network output contains an invalid staging path')
    return 'reuse'


def network_build_compatibility(target, settings, target_abi, compiler, runtime,
                               wpa_backend):
    """Describe only target ABI, compiler/runtime identity, and compile mode."""
    return {
        'format': 'emberbsd-network-build-compatibility',
        'version': 2,
        'target': target,
        'machine': settings['machine'],
        'arch': settings['arch'],
        'triplet': settings['triplet'],
        'target_abi': target_abi,
        'compiler': {
            'family': compiler['family'],
            'version': compiler['version'],
            'triplet': compiler['triplet'],
            'reported_triplet': compiler['reported_triplet'],
        },
        'runtime': {
            'format': runtime['format'],
            'version': runtime['version'],
            'compiler_family': runtime['compiler_family'],
            'llvm_version': runtime['llvm_version'],
            'components': runtime['components'],
            'target': runtime['target'],
            'triplet': runtime['triplet'],
            'machine_arch': runtime['machine_arch'],
            'elf_machine': runtime['elf_machine'],
            'prefix': runtime['prefix'],
        },
        'build_flags': list(settings['flags']) + [
            '-Oz', '-ffunction-sections', '-fdata-sections',
            'LDFLAGS.console-tools=-Wl,--gc-sections'],
        'wpa_backend': wpa_backend or 'default',
    }


def new_network_root(output):
    """Create a fresh sibling staging tree for the next network root."""
    import os
    import tempfile

    output = Path(output)
    if output.is_symlink() or not output.is_dir():
        raise ValueError('network output must be an existing directory')
    staged = Path(tempfile.mkdtemp(prefix='.network-root-stage-', dir=output))
    os.chmod(staged, 0o755)
    return staged


def commit_network_root(staged, destination, staged_spec=None, destination_spec=None,
                        staged_receipt=None, destination_receipt=None):
    """Replace generated root artifacts and receipt after the candidate passes checks."""
    import os
    import shutil
    import tempfile

    staged, destination = Path(staged), Path(destination)
    staged_spec = Path(staged_spec) if staged_spec is not None else None
    destination_spec = Path(destination_spec) if destination_spec is not None else None
    staged_receipt = Path(staged_receipt) if staged_receipt is not None else None
    destination_receipt = Path(destination_receipt) if destination_receipt is not None else None
    if staged.is_symlink() or not staged.is_dir() or destination.is_symlink():
        raise ValueError('network root staging paths must be real directories')
    if staged.parent != destination.parent or (destination.exists() and not destination.is_dir()):
        raise ValueError('network root staging and destination must share an owned output directory')
    if (staged_spec is None) != (destination_spec is None):
        raise ValueError('network root specification paths must be supplied together')
    if staged_spec is not None and (
            staged_spec.is_symlink() or not staged_spec.is_file() or
            destination_spec.is_symlink() or
            (destination_spec.exists() and not destination_spec.is_file()) or
            staged_spec.parent != destination.parent or
            destination_spec.parent != destination.parent):
        raise ValueError('network root specifications must be regular files in the output directory')
    if (staged_receipt is None) != (destination_receipt is None):
        raise ValueError('network build receipt paths must be supplied together')
    if staged_receipt is not None and (
            staged_receipt.is_symlink() or not staged_receipt.is_file() or
            destination_receipt.is_symlink() or
            (destination_receipt.exists() and not destination_receipt.is_file()) or
            staged_receipt.parent != destination.parent or
            destination_receipt.parent != destination.parent):
        raise ValueError('network build receipts must be regular files in the output directory')
    pairs = [(staged, destination)]
    if staged_spec is not None:
        pairs.append((staged_spec, destination_spec))
    if staged_receipt is not None:
        pairs.append((staged_receipt, destination_receipt))
    backups = []
    for _, target in pairs:
        backup = None
        if target.exists():
            if target.is_dir():
                backup = Path(tempfile.mkdtemp(prefix='.network-old-', dir=target.parent))
                backup.rmdir()
            else:
                descriptor, name = tempfile.mkstemp(prefix='.network-old-', dir=target.parent)
                os.close(descriptor)
                os.unlink(name)
                backup = Path(name)
        backups.append(backup)
    moved = []
    try:
        for (_, target), backup in zip(pairs, backups):
            if backup is not None:
                os.replace(target, backup)
            moved.append((target, backup))
        for (candidate, target), _ in zip(pairs, backups):
            os.replace(candidate, target)
    except OSError:
        for (candidate, target), _ in reversed(list(zip(pairs, backups))):
            if target.exists() and not candidate.exists():
                os.replace(target, candidate)
        for target, backup in reversed(moved):
            if backup is not None and backup.exists():
                os.replace(backup, target)
        raise
    for backup in backups:
        if backup is not None:
            if backup.is_dir():
                shutil.rmtree(backup, ignore_errors=True)
            else:
                try:
                    backup.unlink()
                except OSError:
                    pass


def link_if_same(target, link):
    """Create a generated symlink, accepting only an identical prior result."""
    import os

    link = Path(link)
    if link.is_symlink():
        if os.readlink(link) == target:
            return
        raise ValueError('generated link conflicts with an existing link')
    if link.exists():
        raise ValueError('generated link conflicts with an existing path')
    link.symlink_to(target)


def copy_if_different(source, destination):
    """Atomically refresh an owned generated file without truncating it in place."""
    import filecmp
    import os
    import shutil
    import tempfile

    source, destination = Path(source), Path(destination)
    if source.is_symlink() or not source.is_file() or destination.is_symlink():
        raise ValueError('generated copy paths must be regular files')
    if destination.exists() and not destination.is_file():
        raise ValueError('generated copy destination is not a regular file')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        return
    descriptor, temporary = tempfile.mkstemp(prefix=destination.name + '.tmp-',
                                              dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)
    finally:
        if os.path.lexists(temporary):
            os.unlink(temporary)


def apply_metalog_modes(spec, root, destdir, metalog, install_map):
    """Apply target install modes to staged file records without changing host modes."""
    import re

    spec, root, destdir, metalog = map(Path, (spec, root, destdir, metalog))
    if any(path.is_symlink() for path in (spec, root, destdir, metalog)) or \
            not spec.is_file() or not root.is_dir() or not destdir.is_dir() or \
            not metalog.is_file():
        raise ValueError('METALOG mode inputs must be regular files and directories')
    if isinstance(install_map, str):
        pairs = []
        for entry in install_map.split():
            if '=' not in entry:
                raise ValueError('invalid installed program mapping')
            pairs.append(tuple(entry.split('=', 1)))
    else:
        pairs = list(install_map)
    if not pairs:
        return

    records = {}
    for line in metalog.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        records.setdefault(fields[0], []).append(fields[1:])

    spec_lines = spec.read_text().splitlines(keepends=True)
    for _source, target in pairs:
        if not isinstance(target, str) or target.startswith('/') or \
                any(part in ('', '.', '..') for part in target.split('/')):
            raise ValueError('invalid installed target path')
        rel = './' + target
        target_root = root / target
        target_dest = destdir / target
        if target_root.is_symlink() or target_dest.is_symlink() or \
                not target_root.is_file() or not target_dest.is_file():
            raise ValueError('staged program or target install is missing or not regular')
        modes = set()
        for fields in records.get(rel, ()):
            if 'type=file' not in fields:
                continue
            mode_fields = [field[5:] for field in fields if field.startswith('mode=')]
            if len(mode_fields) != 1 or not re.fullmatch(r'[0-7]{3,5}', mode_fields[0]):
                raise ValueError('target METALOG has an invalid file mode for ' + target)
            mode = int(mode_fields[0], 8)
            if mode > 0o7777:
                raise ValueError('target METALOG mode exceeds permission bits')
            modes.add(mode_fields[0])
        if len(modes) != 1:
            raise ValueError('target METALOG mode is missing or inconsistent for ' + target)
        parent = './' + str(Path(target).parent)
        basename = Path(target).name
        current = None
        matching = []
        for index, line in enumerate(spec_lines):
            section = re.fullmatch(r'\s*#\s+(\./[^\s]+)\s*', line.rstrip('\n'))
            if section:
                current = section.group(1)
                continue
            if line.strip() == '..':
                current = None
                continue
            fields = line.split()
            if current == parent and fields and fields[0] == basename:
                matching.append(index)
        if len(matching) != 1:
            raise ValueError('staged root specification lacks a unique target entry for ' + target)
        index = matching[0]
        mode_re = re.compile(r'(?<!\S)mode=[0-7]{3,5}(?=\s|$)')
        updated, count = mode_re.subn('mode=' + next(iter(modes)), spec_lines[index])
        if count != 1:
            raise ValueError('staged root specification has no unique mode for ' + target)
        spec_lines[index] = updated
    spec.write_text(''.join(spec_lines))


def write_if_different(destination, content, mode=0o644):
    """Atomically refresh an owned generated text file."""
    import os
    import tempfile

    destination = Path(destination)
    if destination.is_symlink() or (destination.exists() and not destination.is_file()):
        raise ValueError('generated text destination must be a regular file')
    encoded = content.encode()
    if destination.is_file() and destination.read_bytes() == encoded:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=destination.name + '.tmp-',
                                              dir=destination.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(encoded)
        os.chmod(temporary, mode)
        os.replace(temporary, destination)
    finally:
        if os.path.lexists(temporary):
            os.unlink(temporary)


def stage_overlay(overlay, root, allow_identical=False, allow_suffixes=None,
                  replace_regular=False):
    """Add regular files without replacing base paths or following symlinks."""
    import os
    import shutil
    import stat
    import tempfile

    overlay, root = Path(overlay), Path(root)
    if overlay.is_symlink() or root.is_symlink() or not overlay.is_dir() or not root.is_dir():
        raise ValueError('overlay and root must be existing directories')
    entries = []
    for directory, dirs, files in os.walk(overlay, followlinks=False):
        for name in sorted(dirs + files):
            source = Path(directory) / name
            dest = root / source.relative_to(overlay)
            if source.is_symlink() or not (source.is_dir() or source.is_file()):
                raise ValueError('overlay must contain only regular files and directories')
            identical = False
            if allow_identical and source.is_file() and dest.is_file() and not dest.is_symlink():
                source_bytes, dest_bytes = source.read_bytes(), dest.read_bytes()
                suffix = (allow_suffixes or {}).get(str(source.relative_to(overlay)))
                identical = (source_bytes == dest_bytes or
                             (suffix is not None and dest_bytes == source_bytes + suffix))
            if dest.is_symlink() or (dest.exists() and
                    not (source.is_dir() and dest.is_dir()) and not identical and
                    not (replace_regular and source.is_file() and dest.is_file())):
                raise ValueError('overlay conflicts with a base path')
            entries.append((source, dest, dest.exists(), identical))
    for source, dest, existed, identical in entries:
        if source.is_dir():
            dest.mkdir(exist_ok=True)
        elif not identical:
            if replace_regular and dest.is_file():
                descriptor, temporary = tempfile.mkstemp(
                    prefix=dest.name + '.overlay-tmp-', dir=dest.parent)
                os.close(descriptor)
                try:
                    shutil.copy2(source, temporary)
                    os.replace(temporary, dest)
                finally:
                    if os.path.lexists(temporary):
                        os.unlink(temporary)
            else:
                shutil.copy2(source, dest)
    for source, dest, existed, _ in reversed(entries):
        if source.is_dir() and not existed:
            dest.chmod(stat.S_IMODE(source.stat().st_mode))


def console(target, compact=False):
    targets = {
        'aarch64': ('evbarm', 'aarch64', 'aarch64--netbsd', 'ELF64', 'AArch64', ()),
        'riscv32': ('riscv', 'riscv32', 'riscv32--netbsd', 'ELF32', 'RISC-V', ('MKSOFTFLOAT=yes',)),
    }
    if target not in targets:
        raise ValueError('unsupported static console target')
    machine, arch, triplet, elf_class, elf_machine, flags = targets[target]
    libraries = ('lib/libterminfo', 'lib/libedit', 'lib/libutil')
    programs = ('bin/sh', 'sbin/init')
    if compact:
        libraries += ('lib/libm', 'lib/libprop', 'lib/libkvm')
        programs += ('bin/hostname', 'sbin/sysctl', 'sbin/dmesg',
                     'sbin/ifconfig', 'bin/cat', 'bin/ls', 'bin/chmod',
                     'bin/cp', 'bin/mkdir', 'bin/sleep', 'sbin/reboot')
    return dict(machine=machine, arch=arch, triplet=triplet, elf_class=elf_class,
                elf_machine=elf_machine, flags=flags, headers=('lib/libcurses',),
                libraries=libraries, programs=programs, compact=compact)


def network(target):
    """Select the ordinary-init network userland from the same C foundation."""
    selected = console(target)
    selected.update(profile='network', compact=False)
    selected['headers'] = ('lib/libcurses',)
    selected['libraries'] = ('lib/libcrypt', 'lib/libterminfo', 'lib/libedit',
                             'lib/libm', 'lib/libutil', 'lib/libprop',
                             'lib/libkvm', 'lib/libipsec',
                             'external/bsd/libpcap/lib')
    selected['crunch_programs'] = (
        'bin/sh', 'bin/cat', 'bin/chmod', 'bin/cp', 'bin/date', 'bin/echo', 'bin/expr',
        'bin/hostname', 'bin/kill', 'bin/ln', 'bin/ls', 'bin/mkdir', 'bin/mv',
        'bin/ps', 'bin/rm', 'bin/sleep', 'bin/stty', 'bin/sync', 'sbin/dmesg',
        'sbin/ifconfig', 'sbin/ping', 'sbin/reboot', 'sbin/route', 'sbin/sysctl',
        'usr.bin/fmt', 'usr.bin/getent', 'usr.bin/grep', 'usr.bin/sed',
        'sbin/init', 'sbin/chown', 'sbin/fsck', 'sbin/fsck_ffs', 'sbin/mount',
        'sbin/mount_ffs', 'sbin/mount_tmpfs', 'sbin/rcorder', 'sbin/ttyflags',
        'sbin/umount', 'libexec/getty',
        'external/bsd/dhcpcd/sbin/dhcpcd',
        'external/bsd/wpa/bin/wpa_supplicant', 'external/bsd/wpa/bin/wpa_cli',
        'external/apache2/mDNSResponder/usr.sbin/mdnsd')
    selected['programs'] = ('usr.bin/login',)
    selected['data_components'] = (
        'external/bsd/dhcpcd/libexec/dhcpcd-run-hooks',
        'external/bsd/dhcpcd/libexec/dhcpcd-hooks')
    install_map = [('usr.bin/login', 'usr/bin/login')]
    selected['install_map'] = tuple(install_map)
    selected['device_nodes'] = ('console', 'constty', 'null', 'zero', 'tty', 'klog',
                                'random', 'urandom', 'bpf')
    selected['device_links'] = ('bpf0',)
    return selected


def output_programs(settings, extra=''):
    """Return staged executables whose target ABI and static linkage are checked."""
    if settings['compact']:
        selected = ('bin/console-tools',)
    elif settings.get('profile') == 'network':
        selected = ('bin/console-tools', *(destination for _, destination in settings['install_map']))
    else:
        selected = tuple(pair[1] for pair in settings.get('install_map', ())) or settings['programs']
    return selected + (('bin/' + Path(extra).name,) if extra else ())


def network_device_selection(settings, root_device, root_partition='a', application=False):
    """Select standard network devices and the console-tools GUI additions."""
    import re
    if not re.fullmatch(r'[A-Za-z]+[0-9]*', root_device):
        raise ValueError('network root device must be a device name without a partition')
    if not re.fullmatch(r'[a-p]', root_partition):
        raise ValueError('network root partition must be a single disk partition letter')
    nodes = settings['device_nodes']
    root_partitioned = root_device + root_partition
    nodes += ('r' + root_partitioned,)
    make_devices = ('bpf', root_device)
    block_nodes = (root_partitioned,)
    if application:
        nodes += ('ttyE0', 'wsmouse0', 'iic0')
        make_devices += ('wscons', 'iic0')
    return nodes, settings['device_links'], make_devices, block_nodes


def stage_network(root, source, overlay, interface, pwd_mkdb, dhcpcd_dest,
                  root_device, root_partition='a'):
    """Stage standard init/rc network services over the normal OS defaults."""
    import shutil
    import subprocess
    from pathlib import Path

    root, source, overlay, dhcpcd_dest = (Path(root), Path(source), Path(overlay), Path(dhcpcd_dest))
    if root.is_symlink() or not root.is_dir() or source.is_symlink() or not source.is_dir():
        raise ValueError('network root and source must be existing directories')
    if overlay.is_symlink() or not overlay.is_dir():
        raise ValueError('network overlay must be an existing directory')
    if not __import__('re').fullmatch(r'[A-Za-z0-9_.-]+', interface):
        raise ValueError('network interface must be a plain interface name')
    if not __import__('re').fullmatch(r'[A-Za-z]+[0-9]*', root_device):
        raise ValueError('network root device must be a device name without a partition')
    if not __import__('re').fullmatch(r'[a-p]', root_partition):
        raise ValueError('network root partition must be a single disk partition letter')
    for name in ('bin', 'sbin', 'usr/bin', 'usr/sbin', 'usr/libexec', 'etc/defaults',
                 'etc/rc.network.d', 'tmp', 'var/run', 'var/run/dhcpcd',
                 'var/db/dhcpcd', 'var/log', 'var/tmp', 'root',
                 'libexec/dhcpcd-hooks'):
        (root / name).mkdir(parents=True, exist_ok=True)
    (root / 'var/tmp').chmod(0o1777)
    for name in ('rc', 'rc.subr'):
        copy_if_different(source / 'etc' / name, root / 'etc' / name)
    for name in ('rc.conf', 'gettytab', 'login.conf', 'group', 'master.passwd'):
        copy_if_different(source / 'etc' / name, root / 'etc' / name)
    copy_if_different(source / 'etc/defaults/rc.conf', root / 'etc/defaults/rc.conf')
    dhcpcd_accounts = [line.split(':') for line in
                       (source / 'etc/master.passwd').read_text().splitlines()
                       if line.startswith('_dhcpcd:')]
    if len(dhcpcd_accounts) != 1:
        raise ValueError('OS master.passwd must define the dhcpcd privsep account')
    dhcpcd_home = Path(dhcpcd_accounts[0][8])
    if not dhcpcd_home.is_absolute() or '..' in dhcpcd_home.parts:
        raise ValueError('dhcpcd privsep home must be an absolute in-root path')
    (root / dhcpcd_home.relative_to('/')).mkdir(parents=True, exist_ok=True)
    write_if_different(root / 'etc/ttys',
        'console "/usr/libexec/getty default" vt100 off secure\n'
        'constty "/usr/libexec/getty std.115200" vt100 on secure\n')
    write_if_different(root / 'etc/fstab',
        f'ROOT.{root_partition} / ffs rw 1 1\n'
        'tmpfs /tmp tmpfs rw\n'
        'tmpfs /var/run tmpfs rw\n')
    write_if_different(root / 'etc/rc.conf',
        'rc_configured=YES\n'
        'rc_directories=/etc/rc.network.d\n'
        'critical_filesystems_local="/tmp /var/run"\n'
        'hostname=\n'
        'auto_ifconfig=NO\n'
        f'net_interfaces="{interface}"\n'
        'ipfilter=NO\n'
        'ipsec=NO\n'
        'devpubd=NO\n'
        'no_swap=YES\n'
        'network_tmpdirs=YES\n'
        'wpa_supplicant=YES\n'
        f'wpa_supplicant_flags="-Dbsd -i {interface} -c /etc/wpa_supplicant.conf"\n'
        'dhcpcd=YES\n'
        f'dhcpcd_flags="-b -C wpa_supplicant {interface}"\n'
        'mdnsd=YES\n')
    scripts = ('fsck_root', 'resize_root', 'root', 'devpubd', 'DISKS', 'swap1', 'fsck',
               'mountcritlocal', 'CRITLOCALMOUNTED', 'bootconf.sh', 'ttys', 'sysctl',
               'ipfilter', 'ipsec', 'network', 'staticroute', 'wpa_supplicant',
               'dhcpcd', 'mdnsd', 'NETWORKING', 'DAEMON', 'LOGIN')
    for name in scripts:
        copy_if_different(source / 'etc/rc.d' / name, root / 'etc/rc.network.d' / name)
    runtime_dirs = root / 'etc/rc.network.d/network_tmpdirs'
    runtime_dirs_text = (
        '#!/bin/sh\n'
        '# PROVIDE: network_tmpdirs\n'
        '# REQUIRE: CRITLOCALMOUNTED\n'
        '# BEFORE: wpa_supplicant dhcpcd mdnsd\n'
        '$_rc_subr_loaded . /etc/rc.subr\n'
        'name=network_tmpdirs\n'
        'rcvar=$name\n'
        'start_cmd=network_tmpdirs_start\n'
        'network_tmpdirs_start() { mkdir -p /var/run/dhcpcd; }\n'
        'load_rc_config $name\n'
        'run_rc_command "$1"\n')
    if runtime_dirs.exists():
        if runtime_dirs.is_symlink() or not runtime_dirs.is_file() or \
                runtime_dirs.read_text() != runtime_dirs_text:
            raise ValueError('existing network_tmpdirs script differs from selected profile')
        runtime_dirs.chmod(0o555)
    else:
        write_if_different(runtime_dirs, runtime_dirs_text, mode=0o555)
    stage_overlay(overlay, root, allow_identical=True,
                  allow_suffixes={'etc/ifconfig.' + interface: b'up\n'},
                  replace_regular=True)
    interface_config = root / 'etc' / ('ifconfig.' + interface)
    interface_text = interface_config.read_text() if interface_config.exists() else ''
    interface_lines = interface_text.splitlines()
    if 'up' not in interface_lines:
        with interface_config.open('a') as stream:
            if interface_text and not interface_text.endswith('\n'):
                stream.write('\n')
            stream.write('up\n')
    resolv_conf = root / 'etc/resolv.conf'
    if not resolv_conf.exists():
        write_if_different(resolv_conf, '')
    mdns_accounts = [line for line in (source / 'etc/master.passwd').read_text().splitlines()
                     if line.startswith('_mdnsd:')]
    if len(mdns_accounts) != 1:
        raise ValueError('OS master.passwd must define the mdnsd service account')
    passwd_file = root / 'etc/master.passwd'
    current_mdns = [line for line in passwd_file.read_text().splitlines()
                    if line.startswith('_mdnsd:')]
    if current_mdns and current_mdns != mdns_accounts:
        raise ValueError('network root conflicts with the OS mdnsd account')
    if not current_mdns:
        with passwd_file.open('a') as stream:
            stream.write('\n' + mdns_accounts[0] + '\n')
    subprocess.run([str(pwd_mkdb), '-L', '-p', '-d', str(root),
                    str(root / 'etc/master.passwd')], check=True)
    subprocess.run([str(Path(pwd_mkdb).with_name('nbcap_mkdb')), '-l', '-f',
                    str(root / 'etc/login.conf'), str(root / 'etc/login.conf')], check=True)
    for name in ('passwd', 'pwd.db', 'spwd.db', 'login.conf.db'):
        if not (root / 'etc' / name).is_file():
            raise ValueError('network account staging did not produce etc/' + name)
    generated_hooks = dhcpcd_dest / 'libexec/dhcpcd-run-hooks'
    generated_directory = dhcpcd_dest / 'libexec/dhcpcd-hooks'
    if generated_hooks.is_symlink() or not generated_hooks.is_file() or \
            generated_directory.is_symlink() or not generated_directory.is_dir():
        raise ValueError('native dhcpcd hook install targets are missing')
    copy_if_different(generated_hooks, root / 'libexec/dhcpcd-run-hooks')
    for hook in ('01-test', '20-resolv.conf', '30-hostname', '50-ntp.conf'):
        generated = generated_directory / hook
        if generated.is_symlink() or not generated.is_file():
            raise ValueError('native dhcpcd install is missing hook ' + hook)
        copy_if_different(generated, root / 'libexec/dhcpcd-hooks' / hook)


def check_foundation(record, target, runtime):
    """Require the actual OS C stage and its selected bootstrap runtime."""
    selected = console(target)
    try:
        signature = record['signature']
        flags = signature['flags']
        valid = (record['format'] == 'emberbsd-llvm-os-stage' and
                 record['phase'] == 'base' and
                 signature['target']['arch'] == selected['arch'] and
                 signature['target']['triplet'] == selected['triplet'] and
                 signature['runtime']['manifest'] == runtime and
                 all([flag for flag in flags if flag.split('=', 1)[0] == required.split('=', 1)[0]] == [required]
                     for required in ('MKPIC=no', 'MKPICINSTALL=no', 'MKCOMPAT=no', 'MKCXX=no',
                                      'MKPIE=no', 'LDSTATIC=-static', *selected['flags'])))
    except (KeyError, TypeError):
        valid = False
    if not valid:
        raise ValueError('C foundation does not match the selected target and bootstrap runtime')


def foundation_matches_receipt(current, recorded):
    """Keep receipt outputs intact while allowing selected pkgsrc installs."""
    if not isinstance(current, dict) or not isinstance(recorded, dict):
        return False
    if any(current.get(path) != value for path, value in recorded.items()):
        return False
    additions = current.keys() - recorded.keys()
    return all(path == 'usr/pkg' or path.startswith('usr/pkg/') for path in additions)


def check_compiler_family(family):
    if family != 'clang':
        raise ValueError('static console supports only the LLVM C foundation (clang)')


def check_os_tools(tools, native, search_path, validate, compact=False, network=False):
    names = ('nbmkdep', 'nbmtree') + (('nbpwd_mkdb', 'nbcap_mkdb') if network else ('nbmakefs',))
    if compact or network:
        names += ('nbcrunchgen',)
    for name in names:
        validate(str(tools), str(tools / 'bin' / name), dict(native.host),
                 search_path=search_path)
    if compact or network:
        for name in ('llvm-nm', 'llvm-objcopy'):
            validate(native.bundle_root, str(Path(native.bundle_root) / 'bin' / name),
                     dict(native.host), search_path=search_path)


def check_compiler_identity(actual, recorded):
    keys = ('family', 'version', 'triplet')
    try:
        matches = all(actual[key] == recorded[key] for key in keys)
    except (KeyError, TypeError):
        matches = False
    if not matches:
        raise ValueError('target compiler differs from C foundation')


def source_worktree_state(git, source):
    """Record the tracked diff and untracked bytes without requiring a clean tree."""
    import hashlib
    import os
    import stat
    import subprocess
    from pathlib import Path

    source = Path(source)
    def capture(*args):
        return subprocess.check_output([git, '-C', str(source), *args])
    status = capture('status', '--porcelain=v1', '-z', '--untracked-files=all')
    digest = hashlib.sha256()
    def add(data):
        digest.update(len(data).to_bytes(8, 'big'))
        digest.update(data)
    add(status)
    add(capture('diff', '--cached', '--binary', '--no-ext-diff', '--no-textconv'))
    add(capture('diff', '--binary', '--no-ext-diff', '--no-textconv'))
    names = capture('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')
    for name in sorted(n for n in names if n):
        path = source / os.fsdecode(name)
        mode = path.lstat().st_mode
        add(name)
        add(str(stat.S_IMODE(mode)).encode('ascii'))
        if stat.S_ISLNK(mode):
            add(b'link')
            add(os.fsencode(os.readlink(path)))
        elif stat.S_ISREG(mode):
            add(b'file')
            content = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    content.update(block)
            add(content.digest())
        else:
            raise ValueError('untracked source input is not a regular file or symlink')
    return {'state': 'modified' if status else 'clean', 'sha256': digest.hexdigest()}


def run_console(arguments):
    """Extend a verified C foundation with the shared native console targets."""
    import hashlib
    import json
    import os
    import re
    import shutil
    import subprocess
    import sys
    from pathlib import Path

    if len(arguments) != 4:
        raise SystemExit('usage: cix-p1-native.py SOURCE OUTPUT TOOLDIR TARGET_PREFIX')
    try:
        check_compiler_family(os.environ.get('EMBER_TARGET_COMPILER_FAMILY', 'clang'))
    except ValueError as exc:
        raise SystemExit('static-console: ' + str(exc)) from exc
    required = ('EMBERBSD_SDK_SOURCE', 'EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE',
                'EMBER_HOST_BFD_CONFIG', 'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE',
                'EMBER_BYACC_SOURCE', 'EMBER_C_FOUNDATION', 'EMBER_LLVM_RUNTIME_STAGE',
                'EMBER_CONSOLE_TARGET')
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise SystemExit('missing required inputs: ' + ', '.join(missing))
    source, output, tools, compiler = (Path(value).absolute() for value in arguments)
    target = os.environ['EMBER_CONSOLE_TARGET']
    profile = os.environ.get('EMBER_USERLAND_PROFILE', 'console')
    is_network = profile == 'network'
    settings = network(target) if is_network else console(target, profile == 'console-tools')
    sdk = Path(os.environ['EMBERBSD_SDK_SOURCE']).resolve(strict=True)
    sys.path.insert(0, str(sdk / 'src'))
    from emberbsd_sdk import cpu_capacity, elf, host_prefix, inventory, jsonio, libedit, llvm_runtime, native_os, paths, process, sourceprep, target_toolchain
    from emberbsd_sdk.errors import SDKError
    network_root_stage = None
    network_receipt_pending = None
    try:
        source = Path(paths.canonical_existing(source))
        foundation = Path(paths.canonical_existing(os.environ['EMBER_C_FOUNDATION']))
        stage = Path(paths.canonical_existing(os.environ['EMBER_LLVM_RUNTIME_STAGE']))
        tools = Path(paths.canonical_existing(tools))
        compiler = Path(paths.canonical_existing(compiler))
        if Path(output).is_symlink():
            raise SDKError('output must not be a symlink')
        if Path(output).exists():
            if not Path(output).is_dir():
                raise SDKError('existing output must be a directory')
            output = Path(paths.canonical_existing(output))
        else:
            output = Path(paths.canonical_new(output))
        extra_path_names = ['EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE', 'EMBER_HOST_BFD_CONFIG',
                            'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE', 'EMBER_BYACC_SOURCE']
        if is_network:
            extra_path_names.extend(('EMBER_LIBPCAP_SOURCE', 'EMBER_WPA_SOURCE',
                                     'EMBER_PKGSRC_EXPORT'))
            if os.environ.get('EMBER_LIBPCAP_ARCHIVE'):
                extra_path_names.append('EMBER_LIBPCAP_ARCHIVE')
            if os.environ.get('EMBER_WPA_ARCHIVE'):
                extra_path_names.append('EMBER_WPA_ARCHIVE')
            if os.environ.get('EMBER_WPA_HOSTAPD_ARCHIVE'):
                extra_path_names.append('EMBER_WPA_HOSTAPD_ARCHIVE')
        missing = [name for name in extra_path_names if not os.environ.get(name)]
        if missing:
            raise SDKError('missing required inputs: ' + ', '.join(missing))
        extra_paths = [Path(paths.canonical_existing(os.environ[name])) for name in extra_path_names]
        for item in (source, foundation, stage, tools, compiler, output, sdk, *extra_paths):
            if not re.fullmatch(r'/[A-Za-z0-9_./-]+', str(item)):
                raise SDKError('paths must be absolute and contain plain path characters')
        for root in (source, foundation, stage, tools, compiler, sdk):
            paths.reject_overlap(str(output), str(root))
        host = os.environ['EMBER_HOST_PREFIX']
        native = native_os.select_native_os_tools(host, str(source),
            compiler_family=os.environ.get('EMBER_HOST_COMPILER_FAMILY', 'clang'))
        git = os.environ.get('EMBER_GIT') or shutil.which('git')
        if not git or not os.path.isabs(git):
            raise SDKError('select an absolute Git executable')
        def git_output(*args):
            return subprocess.check_output([git, '-C', str(source), *args], text=True).strip()
        revision = git_output('rev-parse', 'HEAD')
        worktree = source_worktree_state(git, source)
        timestamp = git_output('show', '-s', '--format=%ct', revision)
        receipt = jsonio.read_json(foundation / '.prebuild-sdk-build.json')
        original = receipt['sources']['os']['commit']
        foundation_exclude = ('.prebuild-sdk-build.json', '.prebuild-sdk-build-incomplete',
                              '.prebuild-sdk-build.log')
        foundation_state = inventory.snapshot_tree(foundation, exclude=foundation_exclude)
        if not foundation_matches_receipt(foundation_state, receipt['outputs']):
            raise SDKError('C foundation contents changed')
        record = llvm_runtime.validate_stage(stage, target=target,
            required_components=('builtins', 'libunwind-static'), base=foundation)
        check_foundation(receipt, target, record)
        env = native_os.native_os_environment(native, output / 'routing')
        check_os_tools(tools, native, env['PATH'], host_prefix.validate_host_executable,
                       compact=settings['compact'], network=is_network)
        identity = target_toolchain.clang_compiler(str(compiler), settings['triplet'],
                                                    host=dict(native.host), env=env)
        check_compiler_identity(identity, receipt['signature']['compiler'])
        overlay = (os.environ.get('EMBER_NETWORK_OVERLAY', '') if is_network else
                   os.environ.get('EMBER_CONSOLE_OVERLAY', '') if settings['compact'] else '')
        network_interface = os.environ.get('EMBER_NETWORK_INTERFACE', '')
        network_root_device = os.environ.get('EMBER_NETWORK_ROOT_DEVICE', '')
        network_root_partition = os.environ.get('EMBER_NETWORK_ROOT_PARTITION', 'a')
        wpa_backend = os.environ.get('EMBER_WPA_CRYPTO_BACKEND', '') if is_network else ''
        wolfssl_prefix = os.environ.get('EMBER_WOLFSSL_PREFIX', '') if is_network else ''
        network_sources = {}
        wolfssl_hash = ''
        libpcap_archive_hash = ''
        if is_network:
            pkgsrc_export = os.environ['EMBER_PKGSRC_EXPORT']
            export_receipt = jsonio.read_json(
                Path(pkgsrc_export) / 'pkgsrc-export.json')
            patch_tool = export_receipt['identity']['patch']['path']
            patch_tool = host_prefix.validate_host_executable(
                host, patch_tool, dict(native.host), search_path=env['PATH'])
            if not network_interface:
                raise SDKError('EMBER_NETWORK_INTERFACE must select the target interface')
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', network_interface):
                raise SDKError('EMBER_NETWORK_INTERFACE must be an interface name')
            if not re.fullmatch(r'[A-Za-z]+[0-9]*', network_root_device):
                raise SDKError('EMBER_NETWORK_ROOT_DEVICE must select a device name without a partition')
            if not re.fullmatch(r'[a-p]', network_root_partition):
                raise SDKError('EMBER_NETWORK_ROOT_PARTITION must select a disk partition letter')
            if not overlay:
                raise SDKError('EMBER_NETWORK_OVERLAY must name the selected runtime overlay')
            if wolfssl_prefix:
                wolfssl_prefix = paths.canonical_existing(wolfssl_prefix)
                if (not re.fullmatch(r'/[A-Za-z0-9_./-]+', wolfssl_prefix) or
                    Path(wolfssl_prefix).parts[-2:] != ('usr', 'pkg') or
                    Path(wolfssl_prefix).is_relative_to(extra_paths[0]) or
                    not Path(wolfssl_prefix, 'lib/libwolfssl.a').is_file()):
                    raise SDKError('EMBER_WOLFSSL_PREFIX must contain the selected target libwolfssl.a')
                wolfssl_hash = hashlib.sha256(
                    Path(wolfssl_prefix, 'lib/libwolfssl.a').read_bytes()).hexdigest()
                paths.reject_overlap(str(output), wolfssl_prefix)
            for variable in ('EMBER_MDNS_SOURCE', 'EMBER_DHCPCD_SOURCE',
                             'EMBER_LIBPCAP_SOURCE', 'EMBER_WPA_SOURCE'):
                if not os.environ.get(variable):
                    raise SDKError('missing required network source: ' + variable)
                selected_source = paths.canonical_existing(os.environ[variable])
                if not re.fullmatch(r'/[A-Za-z0-9_./-]+', selected_source):
                    raise SDKError(variable + ' must be an absolute plain path')
                paths.reject_overlap(str(output), selected_source)
                if variable == 'EMBER_MDNS_SOURCE':
                    entry, package = 'scripts/source-inputs/mdns/source.py', 'mdns'
                elif variable == 'EMBER_DHCPCD_SOURCE':
                    entry, package = 'scripts/source-inputs/dhcpcd/source.py', 'dhcpcd'
                elif variable == 'EMBER_WPA_SOURCE':
                    entry, package = 'scripts/source-inputs/wpa/source.py', 'wpa'
                else:
                    entry, package = 'scripts/source-inputs/libpcap/source.py', 'libpcap'
                verifier_source = str(Path(os.environ['EMBER_PORTS_SOURCE']) / entry)
                verify_command = [native.tool('python'), '-B', verifier_source,
                    '--sdk-source', str(sdk), '--git', git, 'verify-source',
                    '--source', selected_source, '--patch', patch_tool,
                    '--pkgsrc-export', pkgsrc_export]
                if package == 'libpcap' and os.environ.get('EMBER_LIBPCAP_ARCHIVE'):
                    archive = Path(paths.canonical_existing(
                        os.environ['EMBER_LIBPCAP_ARCHIVE']))
                    if not archive.is_file() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', str(archive)):
                        raise SDKError('EMBER_LIBPCAP_ARCHIVE must name an absolute archive file')
                    paths.reject_overlap(str(output), str(archive))
                    libpcap_archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
                    verify_command.extend(('--archive', str(archive)))
                if package == 'wpa' and ('EMBER_WPA_ARCHIVE' in os.environ or
                                         'EMBER_WPA_HOSTAPD_ARCHIVE' in os.environ):
                    if not (os.environ.get('EMBER_WPA_ARCHIVE') and
                            os.environ.get('EMBER_WPA_HOSTAPD_ARCHIVE')):
                        raise SDKError('WPA archive comparison requires both WPA and hostapd archives')
                    for variable in ('EMBER_WPA_ARCHIVE', 'EMBER_WPA_HOSTAPD_ARCHIVE'):
                        archive = Path(paths.canonical_existing(os.environ[variable]))
                        if not archive.is_file() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', str(archive)):
                            raise SDKError(variable + ' must name an absolute archive file')
                        paths.reject_overlap(str(output), str(archive))
                        verify_command.extend((('--archive' if variable == 'EMBER_WPA_ARCHIVE'
                                                 else '--hostapd-archive'), str(archive)))
                verified = process.capture(verify_command, env=env).stdout
                network_sources[package] = hashlib.sha256(verified).hexdigest()
                os.environ[variable] = selected_source
        extra = os.environ.get('EMBER_CONSOLE_PROGRAM', '') if settings['compact'] or is_network else ''
        network_devices = network_device_selection(settings, network_root_device,
            network_root_partition, bool(extra)) if is_network else None
        overlay_state = None
        extra_digest = None
        if overlay:
            overlay = paths.canonical_existing(overlay)
            paths.reject_overlap(str(output), overlay)
            if not Path(overlay).is_dir() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', overlay):
                raise SDKError('console overlay must be an absolute plain-path directory')
            overlay_state = inventory.snapshot_tree(overlay)
        if extra:
            extra = paths.canonical_existing(extra)
            paths.reject_overlap(str(output), extra)
            if not Path(extra).is_file() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', extra):
                raise SDKError('console program must be an absolute plain-path file')
            if elf.read_elf_header(extra, receipt['signature']['target']['elf'], 2,
                                   riscv32_soft_float=target == 'riscv32') is None:
                raise SDKError('console program has incompatible target ABI')
            segments = process.capture([str(compiler / 'bin/llvm-readelf'), '-l', extra], env=env).stdout.decode()
            if re.search(r'^\s*(?:INTERP|DYNAMIC)\s', segments, re.M):
                raise SDKError('console program must be statically linked')
            extra_digest = hashlib.sha256(Path(extra).read_bytes()).hexdigest()
        libedit.validate_source(os.environ['EMBER_LIBEDIT_SOURCE'],
                                ports=os.environ['EMBER_PORTS_SOURCE'], git=git)
        sourceprep.read_prepared_tree_receipt(os.environ['EMBER_BYACC_SOURCE'], package='devel/byacc')
        current = str(Path(os.environ['EMBER_PORTS_SOURCE']) / 'scripts/prepared-source-current.py')
        process.capture([native.tool('python'), '-B', current, '--sdk-source', str(sdk), '--git', git,
                         '--source', 'byacc=' + os.environ['EMBER_BYACC_SOURCE']], env=env)
        verifier = str(Path(os.environ['EMBER_PORTS_SOURCE']) / 'pkgsrc/lang/compiler-rt/files/runtime.py')
        source_record = jsonio.loads_json(process.capture([native.tool('python'), '-B', verifier,
            '--sdk-source', str(sdk), 'verify-source', '--source', os.environ['EMBER_LLVM_SOURCE'],
            '--git', git], env=env).stdout)
        if (source_record['llvm_version'] != identity['version'] or
                record['source'] != {key: source_record['source'][key] for key in ('archive', 'sha512')}):
            raise SDKError('LLVM source differs from the selected runtime')
        command = [native.tool('python'), '-B', verifier, '--sdk-source', str(sdk), 'verify',
                   '--stage', str(stage), '--target', target, '--phase', 'bootstrap', '--base', str(foundation),
                   '--source', os.environ['EMBER_LLVM_SOURCE'], '--git', git]
        original_source = runtime_original_source(
            receipt, os.environ.get('EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE', ''))
        if original_source:
            command += ['--original-source', original_source]
        process.capture(command, env=env)
        selected = llvm_runtime.runtime_paths(stage, record)
        jobs = str(cpu_capacity.resolve_jobs(os.environ.get('BUILD_JOBS'), option='BUILD_JOBS'))
        output_action = 'new'
        if is_network:
            compatibility = network_build_compatibility(
                target, settings, receipt['signature']['target'], identity, record,
                wpa_backend)
            wrapper_checks = [
                '-m ' + settings['machine'], '-a ' + settings['arch'],
                '-O ' + str(output / 'obj'), '-D ' + str(output / 'dest'),
                '-T ' + str(tools), '-V EXTERNAL_TOOLCHAIN=' + str(compiler),
                '-V EMBER_EXTERNAL_LLVM_RUNTIME=' + selected['runtime-prefix'],
                '-V EMBER_LLVM_RESOURCE_DIR=' + selected['llvm-resource'],
                '-V EMBER_COMPILER_FAMILY=' + compatibility['compiler']['family'],
            ]
            wrapper_checks.extend('-V ' + flag for flag in settings['flags'])
            try:
                output_action = network_output_action(output, compatibility, wrapper_checks,
                    ('-V COPTS=-Oz -ffunction-sections -fdata-sections',))
            except ValueError as exc:
                raise SDKError(str(exc)) from exc
        elif output.exists():
            raise SDKError('output must not already exist')
        if output_action == 'new':
            output.mkdir()
            (output / 'logs').mkdir()
            shutil.copytree(foundation, output / 'dest', symlinks=True,
                ignore=shutil.ignore_patterns('.prebuild-sdk-build.json', '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log'))
        else:
            (output / 'logs').mkdir(exist_ok=True)
        if is_network:
            network_root_stage = new_network_root(output)
        shell = str(Path(__file__).with_name('cix-p1-build-userland.sh'))
        command = [shell, '--console-selected', str(source), str(output), str(tools), str(compiler), str(sdk),
                   native.bundle_root, os.environ.get('EMBER_HOST_COMPILER_FAMILY', 'clang'),
                   settings['machine'], settings['arch'], os.environ['EMBER_LIBEDIT_SOURCE'],
                   os.environ['EMBER_LLVM_SOURCE'], os.environ['EMBER_HOST_BFD_CONFIG'], os.environ['EMBER_BYACC_SOURCE'],
                   selected['runtime-prefix'], selected['llvm-resource'], selected['llvm-unwind'],
                   identity['resource_dir'], jobs, timestamp, revision,
                   ' '.join(settings['headers']), ' '.join(settings['libraries']), ' '.join(settings['programs']),
                   native.tool('python'), profile, overlay, extra, native.tool('awk'), wolfssl_prefix,
                   network_interface, os.environ.get('EMBER_MDNS_SOURCE', ''),
                   os.environ.get('EMBER_DHCPCD_SOURCE', ''),
                   ' '.join('='.join(pair) for pair in settings.get('install_map', ())),
                   os.environ.get('EMBER_WPA_CRYPTO_BACKEND', '') if is_network else '',
                   ' '.join(settings.get('crunch_programs', ())),
                   ' '.join(network_devices[0]) if is_network else '',
                   ' '.join(network_devices[1]) if is_network else '',
                   os.environ.get('EMBER_LIBPCAP_SOURCE', '') if is_network else '',
                   ' '.join(network_devices[2]) if is_network else '',
                   os.environ.get('EMBER_WPA_SOURCE', '') if is_network else '',
                   ' '.join(network_devices[3]) if is_network else '',
                   network_root_device if is_network else '',
                   network_root_partition if is_network else '',
                   str(network_root_stage) if network_root_stage is not None else '',
                   ' '.join(settings.get('data_components', ())) if is_network else '']
        orchestrator_log = network_orchestrator_log(output) if is_network else output / 'logs/orchestrator.log'
        native_os.run_native_os(native, source=str(source), routing=str(output / 'routing'), cwd=str(source),
                                arguments=command, shell=True, log=str(orchestrator_log))
        candidate_root = network_root_stage if is_network else None
        if is_network and (
                candidate_root is None or candidate_root.is_symlink() or not candidate_root.is_dir() or
                Path(str(candidate_root) + '.spec').is_symlink() or
                not Path(str(candidate_root) + '.spec').is_file()):
            raise SDKError('network root staging did not produce a tree and device specification')
        readelf = str(compiler / 'bin/llvm-readelf')
        for name in output_programs(settings, extra):
            binary = ((candidate_root if is_network else output / 'root')
                      if settings['compact'] or is_network else output / 'dest') / name
            header = elf.read_elf_header(binary, receipt['signature']['target']['elf'], 2,
                                         riscv32_soft_float=target == 'riscv32')
            if header is None or header['class'] != (32 if target == 'riscv32' else 64):
                raise SDKError('invalid console executable: ' + name)
            segments = process.capture([readelf, '-l', str(binary)], env=env).stdout.decode()
            if re.search(r'^\s*(?:INTERP|DYNAMIC)\s', segments, re.M):
                raise SDKError('console executable is not static: ' + name)
        if git_output('rev-parse', 'HEAD') != revision or source_worktree_state(git, source) != worktree:
            raise SDKError('OS source changed during build')
        if overlay and inventory.snapshot_tree(overlay) != overlay_state:
            raise SDKError('console overlay changed during build')
        if libpcap_archive_hash and hashlib.sha256(
                Path(os.environ['EMBER_LIBPCAP_ARCHIVE']).read_bytes()).hexdigest() != libpcap_archive_hash:
            raise SDKError('libpcap archive changed during build')
        if extra and hashlib.sha256(Path(extra).read_bytes()).hexdigest() != extra_digest:
            raise SDKError('console program changed during build')
        if inventory.snapshot_tree(foundation, exclude=foundation_exclude) != foundation_state:
            raise SDKError('C foundation or selected package input changed during build')
        receipt_text = ('source=' + revision + '\ntarget=' + target +
            '\nsource-state=' + worktree['state'] + '\nsource-worktree-sha256=' + worktree['sha256'] +
            '\nprofile=static-' + profile + '\nfoundation=' + original + '\n' +
            ('overlay-sha256=' + hashlib.sha256(json.dumps(overlay_state, sort_keys=True).encode()).hexdigest() + '\n'
             if overlay_state is not None else '') +
            ('program-sha256=' + extra_digest + '\n' if extra_digest else '') +
            (f'network-interface={network_interface}\nwpa-crypto-backend={wpa_backend or "default"}\n' +
             (f'wolfssl-sha256={wolfssl_hash}\n' if wolfssl_hash else '') +
             (f'libpcap-archive-sha256={libpcap_archive_hash}\n' if libpcap_archive_hash else '') +
             ''.join(f'{name}-source-record-sha256={digest}\n' for name, digest in sorted(network_sources.items()))
             if is_network else ''))
        if is_network:
            network_receipt_pending = output / 'BUILD-INPUTS.pending'
            if network_receipt_pending.is_symlink() or (network_receipt_pending.exists() and
                                                        not network_receipt_pending.is_file()):
                raise SDKError('network build receipt staging path is not a regular file')
            network_receipt_pending.write_text(receipt_text)
            commit_network_root(network_root_stage, output / 'root',
                                Path(str(network_root_stage) + '.spec'),
                                output / 'root.spec', network_receipt_pending,
                                output / 'BUILD-INPUTS.txt')
            jsonio.write_json(output / 'NETWORK-COMPAT.json', compatibility,
                              exclusive=False)
        else:
            (output / 'BUILD-INPUTS.txt').write_text(receipt_text)
        if is_network:
            print('Staged the static network root tree; no filesystem image was built.')
        else:
            image = output / 'root.fs'
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            (output / 'ROOT-SHA256.txt').write_text(digest + '  root.fs\n')
            print('Built static bring-up console and 4 MiB FFS image; target execution not checked.')
    except (SDKError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        if network_root_stage is not None:
            staged = Path(network_root_stage)
            if staged.parent == output and staged.name.startswith('.network-root-stage-'):
                if staged.is_dir() and not staged.is_symlink():
                    shutil.rmtree(staged)
                staged_spec = Path(str(staged) + '.spec')
                if staged_spec.is_file() and not staged_spec.is_symlink():
                    staged_spec.unlink()
        if network_receipt_pending is not None and network_receipt_pending.is_file() and not network_receipt_pending.is_symlink():
            network_receipt_pending.unlink()
        raise SystemExit('static-console: ' + str(exc)) from exc


if __name__ == '__main__':
    import sys
    if len(sys.argv) == 4 and sys.argv[1] == 'stage-overlay':
        stage_overlay(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == 'link-if-same':
        link_if_same(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 7 and sys.argv[1] == 'apply-metalog-modes':
        apply_metalog_modes(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif len(sys.argv) == 10 and sys.argv[1] == 'stage-network':
        stage_network(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6],
                      sys.argv[7], sys.argv[8], sys.argv[9])
    else:
        raise SystemExit('usage: static-profile.py stage-overlay DIRECTORY ROOT | link-if-same TARGET LINK | stage-network ROOT SOURCE OVERLAY INTERFACE PWD_MKDB DHCPCD_SOURCE ROOT_DEVICE ROOT_PARTITION')
