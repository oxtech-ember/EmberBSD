#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise the network profile selector and standard-root staging contract."""
import importlib.util
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import tty


path = Path(__file__).with_name('static-profile.py')
spec = importlib.util.spec_from_file_location('static_profile', path)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
source = Path(__file__).resolve().parents[2]
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise SystemExit('FAIL: ' + message)


def staged_rc_dependencies(root):
    """Read provider/requirement metadata from the selected staged rc scripts."""
    scripts = sorted((root / 'etc/rc.network.d').iterdir())
    provides = {}
    requirements = {}
    for script in scripts:
        if not script.is_file():
            continue
        metadata = {}
        for line in script.read_text().splitlines():
            if line.startswith('# PROVIDE:'):
                metadata['PROVIDE'] = line.split(':', 1)[1].split()
            elif line.startswith('# REQUIRE:'):
                metadata['REQUIRE'] = line.split(':', 1)[1].split()
        for name in metadata.get('PROVIDE', (script.name,)):
            if name in provides:
                raise SystemExit('FAIL: duplicate rc provider ' + name)
            provides[name] = script.name
        requirements[script.name] = metadata.get('REQUIRE', ())
    missing = sorted({name for names in requirements.values() for name in names
                      if name not in provides})
    return provides, requirements, missing


def syslog_has_forward_action(config):
    for raw in config.splitlines():
        line = raw.split('#', 1)[0].strip()
        fields = line.split()
        if len(fields) < 2 or fields[0].startswith(('!', '+', '-')):
            continue
        if fields[-1].startswith('@'):
            return True
    return False


rc_source = (source / 'etc/rc').read_text()
status_helper_start = rc_source.index('rc_status_display_metadata()\n{')
status_helper_end = rc_source.index('\n}\n\n#\n# rc_postprocess_metadata', status_helper_start)
status_helper = rc_source[status_helper_start:status_helper_end + 2]
plain_handler_start = rc_source.index('rc_postprocess_plain_line()\n{')
plain_handler_end = rc_source.index('\n}\n', plain_handler_start)
check('rc_status_display_metadata stage "$1"' in rc_source and
      'rc_status_display_metadata result "$1" "$2"' in rc_source and
      'rc_status_display' not in rc_source[plain_handler_start:plain_handler_end],
      'rc sends stage metadata to the display')

master, slave = os.openpty()
try:
    tty.setraw(slave)
    calls = """
rc_status_display_metadata stage /etc/rc.d/sshd
rc_status_display_metadata result /etc/rc.d/sshd 0
rc_status_display_metadata result 'bad name' 3
rc_status_display_metadata stop 'unexpected exit text'
rc_status_display_metadata stop 7
rc_status_display_metadata finish success
"""
    run = subprocess.run(['/bin/sh', '-c', status_helper + calls], check=True,
                         capture_output=True,
                         env=dict(os.environ, rc_status_display=os.ttyname(slave)))
    screen = bytearray()
    while select.select([master], [], [], 0)[0]:
        screen.extend(os.read(master, 4096))
    check(not run.stdout and not run.stderr and
          screen.splitlines() == [b'> sshd', b'ok sshd', b'FAIL script rc=3',
                                  b'Startup stopped', b'Startup stopped rc=7',
                                  b'Startup complete'],
          'secondary tty shows sanitized stages and stop codes')
finally:
    os.close(slave)
    os.close(master)


def has_multiuser_rc_dependencies(root):
    provides, requirements, missing = staged_rc_dependencies(root)
    return (not missing and
            {'NETWORKING', 'mountcritremote', 'SERVERS', 'DAEMON', 'LOGIN'} <=
            set(provides) and
            {'NETWORKING', 'root', 'CRITLOCALMOUNTED'} <=
            set(requirements['mountcritremote']) and
            'mountcritremote' in requirements['SERVERS'] and
            {'NETWORKING', 'SERVERS'} <= set(requirements['DAEMON']) and
            'DAEMON' in requirements['LOGIN'])


selected = profile.network('riscv32')
usb = profile.network('earmv7hf', ('usb',))
usb_controllers = {'usb' + str(unit) for unit in range(16)}
check('usr.sbin/usbdevs' in usb['crunch_programs'] and
      'usr.sbin/usbdevs' not in selected['crunch_programs'] and
      usb_controllers | {'drvctl'} <= set(usb['device_nodes']) and
      {'drvctl', 'usbs'} <= set(usb['device_make_groups']) and
      usb_controllers.isdisjoint(selected['device_nodes']),
      'USB diagnostics select usbdevs, drvctl and the standard MAKEDEV USB controller nodes')
littlefs = profile.network('riscv32', root_filesystem='littlefs')
wpa_selected = 'external/bsd/wpa/bin/wpa_supplicant' in selected['crunch_programs']
wpa_source = Path(os.environ['EMBER_WPA_SOURCE']) if wpa_selected and os.environ.get('EMBER_WPA_SOURCE') else None
check(not wpa_selected or wpa_source is not None,
      'the prepared WPA source is required only when the selected network profile includes WPA')
check('sys/net80211' in selected['headers'] and
      'sys/net80211' in littlefs['headers'],
      'network profile stages the current net80211 userland ABI headers')
try:
    mdns_toolset = profile.network('riscv32', ('mdns',))
except ValueError:
    mdns_toolset = {}
optional_programs = {
    'bin/df', 'usr.bin/env', 'usr.bin/uname',
    'usr.bin/audio/ctl/audioctl', 'usr.bin/audio/play/audioplay',
    'usr.bin/audio/record/audiorecord', 'usr.bin/mixerctl/mixerctl',
    'usr.bin/timeout', 'usr.bin/wc', 'usr.bin/videoctl/videoctl',
    'usr.sbin/envstat/envstat', 'usr.sbin/spinandctl/spinandctl',
    'usr.bin/cksum/cksum', 'usr.bin/uuencode/uuencode',
    'usr.bin/uudecode/uudecode', 'usr.bin/logger', 'usr.bin/tail',
    'external/apache2/mDNSResponder/usr.sbin/mdnsd',
}
check(not optional_programs.intersection(selected['crunch_programs']) and
      mdns_toolset and
      'external/apache2/mDNSResponder/usr.sbin/mdnsd' in mdns_toolset['crunch_programs'],
      'network defaults omit optional tools and explicit mdns selection adds mdnsd')
ssh_toolset = profile.network('riscv32', ('ssh',))
ssh_builtin = profile.network('riscv32', ('ssh',), wpa_backend='wolfssl',
                              openssh_crypto='builtin')
dropbear = profile.network('riscv32', ('ssh',), wpa_backend='wolfssl', ssh_server='dropbear')
check(dropbear['openssh_crypto'] == 'not-selected' and
      not any('openssh' in path or 'openssl' in path
              for path in dropbear['programs'] + dropbear['libraries']) and
      'sbin/mount_ptyfs' in dropbear['crunch_programs'] and
      {'ptm', 'ptmx'} <= set(dropbear['device_nodes']) and
      'usr/pkg/sbin/dropbear' in profile.output_programs(dropbear),
      'static Dropbear uses the selected package, PTYs and target ELF checks')
sftp_toolset = profile.network('riscv32', ('ssh', 'sftp'))
logging_toolset = profile.network('riscv32', ('logging',))
logging_wolfssl = profile.network('riscv32', ('logging',), wpa_backend='wolfssl')
display_toolset = profile.network('riscv32', ('display',))
combined_services = profile.network('riscv32', ('ssh', 'logging'))
try:
    profile.network('riscv32', ('sftp',))
except ValueError:
    sftp_requires_ssh = True
else:
    sftp_requires_ssh = False
ssh_install_paths = {dest for _, dest in ssh_toolset['install_map']}
ssh_nodes = profile.network_device_selection(ssh_toolset, 'md0', 'a')[0]
check(not {'usr/sbin/sshd', 'usr/sbin/syslogd', 'usr/bin/newsyslog'} &
      {dest for _, dest in selected['install_map']} and
      {'usr/sbin/sshd', 'usr/libexec/sshd-auth', 'usr/libexec/sshd-session',
       'usr/bin/ssh-keygen'} <= ssh_install_paths and
      'usr/libexec/sftp-server' not in ssh_install_paths and
      'usr/libexec/sftp-server' in
      {dest for _, dest in sftp_toolset['install_map']} and
      'sbin/mount_ptyfs' in ssh_toolset['crunch_programs'] and
      {'ptm', 'ptmx'} <= set(ssh_toolset['device_nodes']) and
      not any('openssh/bin/ssh' == item or item.endswith('/scp')
              for item in ssh_toolset['programs']) and
      'sbin/mount_ptyfs' not in ssh_install_paths and
      ssh_toolset['openssh_crypto'] == 'openssl' and
      ssh_toolset['libraries'].index('lib/libpthread') <
      ssh_toolset['libraries'].index('crypto/external/apache2/openssl/lib/libcrypto') and
      max(ssh_toolset['libraries'].index('crypto/external/apache2/openssl/lib/libcrypto'),
          ssh_toolset['libraries'].index('lib/libz')) <
      ssh_toolset['libraries'].index('crypto/external/bsd/openssh/lib') and
      'crypto/external/apache2/openssl/lib/libssl' not in ssh_toolset['libraries'] and
      len(combined_services['libraries']) == len(set(combined_services['libraries'])),
      'SSH server and SFTP are opt-in, with no client/scp and deduplicated library closure')
check('crypto/external/bsd/openssh/bin/sftp-server' not in ssh_toolset['programs'] and
      'crypto/external/bsd/openssh/bin/sftp-server' in sftp_toolset['programs'],
      'SFTP subsystem is a separate opt-in toolset')
check('external/bsd/blocklist/include' in ssh_toolset['headers'] and
      'external/bsd/blocklist/include' in logging_toolset['headers'] and
      'external/bsd/blocklist/include' not in profile.network('riscv32')['headers'] and
      'external/bsd/libevent/include' in logging_toolset['headers'] and
      'external/bsd/libevent/include' not in profile.network('riscv32')['headers'] and
      ssh_toolset['libraries'].index('external/bsd/blocklist/lib') <
      ssh_toolset['libraries'].index('lib/libwrap') and
      logging_toolset['libraries'].index('external/bsd/blocklist/lib') <
      logging_toolset['libraries'].index('lib/libwrap'),
      'blocklist header and library providers precede selected libwrap consumers')
check(ssh_builtin['openssh_crypto'] == 'builtin' and
      'crypto/external/apache2/openssl/lib/libcrypto' not in ssh_builtin['libraries'] and
      'crypto/external/apache2/openssl/lib/libssl' not in ssh_builtin['libraries'] and
      'crypto/external/bsd/openssh/lib' in ssh_builtin['libraries'] and
      'lib/libpthread' in ssh_builtin['libraries'],
      'built-in SSH crypto avoids OpenSSL while the selected WolfSSL Wi-Fi backend keeps pthread')
check({'ptm', 'ptmx'} <= set(ssh_nodes) and
      'makedev std fd ptm' in (source / 'etc/MAKEDEV.tmpl').read_text() and
      'sbin/mount_ptyfs' in ssh_toolset['crunch_programs'],
      'SSH reuses standard ptm devices and crunches the ptyfs mount helper')
check(not {'usr/sbin/syslogd', 'usr/bin/newsyslog'} &
      {dest for _, dest in logging_toolset['install_map']} and
      {'usr.sbin/syslogd/syslogd', 'usr.bin/newsyslog/newsyslog',
       'usr.bin/logger', 'usr.bin/tail'} <= set(logging_toolset['crunch_programs']) and
      'external/bsd/libevent/lib/libevent' in logging_toolset['libraries'] and
      'crypto/external/apache2/openssl/lib/libcrypto' in logging_toolset['libraries'] and
      'crypto/external/apache2/openssl/lib/libssl' not in logging_toolset['libraries'] and
      logging_toolset['libraries'].index('lib/libpthread') <
      logging_toolset['libraries'].index('crypto/external/apache2/openssl/lib/libcrypto'),
      'logging crunches syslogd, bounded rotation, logger and tail')
check(sftp_requires_ssh and
      'crypto/external/bsd/openssh/bin/sftp-server' not in ssh_toolset['programs'] and
      'crypto/external/bsd/openssh/bin/sftp-server' in sftp_toolset['programs'],
      'SFTP is separately selectable and requires the SSH server')
check(logging_wolfssl['syslogd_tls'] == 'no' and
      logging_wolfssl['syslogd_sign'] == 'no' and
      'external/bsd/libevent/lib/libevent' in logging_wolfssl['libraries'] and
      'crypto/external/apache2/openssl/lib/libcrypto' not in logging_wolfssl['libraries'] and
      'crypto/external/apache2/openssl/lib/libssl' not in logging_wolfssl['libraries'],
      'local diagnostic logging disables TLS/signing and retains no OpenSSL closure')
display_devices = profile.network_device_selection(display_toolset, 'md0')
check('ttyE0' in display_devices[0] and display_devices[2] ==
      ('bpf', 'md0', 'wscons') and
      not {'wsmouse0', 'iic0'} & set(display_devices[0]),
      'the display toolset adds only ttyE0 and its wscons MAKEDEV group')
with tempfile.TemporaryDirectory(prefix='network-crunch-config-') as temporary:
    config = Path(temporary) / 'console-tools.conf'
    generated = subprocess.run(
        [sys.executable, '-B', str(path), 'network-crunch-config', str(config),
         'wolfssl', ' '.join(logging_toolset['crunch_programs'])],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    lines = config.read_text().splitlines() if config.is_file() else []
    flags = subprocess.run(
        [sys.executable, '-B', str(path), 'network-crunch-makeflags',
         ' '.join(logging_toolset['crunch_programs'])],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, check=False)
    no_logging_flags = subprocess.run(
        [sys.executable, '-B', str(path), 'network-crunch-makeflags',
         ' '.join(ssh_toolset['crunch_programs'])],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, check=False)
    check(generated.returncode == 0 and
          'libs -ledit -lterminfo -lutil -lkvm -lprop -lcrypt -lpcap -lipsec -lm -lwolfssl -lpthread -levent -lwrap -lblocklist' in lines and
          'special syslogd keepsymbols allow_severity deny_severity' in lines and
          flags.returncode == 0 and
          {'SYSLOGD_TLS=no', 'SYSLOGD_SIGN=no'} <= set(flags.stdout.split()) and
          no_logging_flags.returncode == 0 and
          not {'SYSLOGD_TLS=no', 'SYSLOGD_SIGN=no'} & set(no_logging_flags.stdout.split()),
          'the emitted crunch config separates syslogd symbol directives and forwards only selected flags')
check('sbin/mount_littlefs' in littlefs['crunch_programs'] and
      'sys/sys' in littlefs['headers'] and
      'sys/fs/littlefs' in littlefs['headers'] and
      'sbin/mount_ffs' not in littlefs['crunch_programs'] and
      'sbin/fsck_ffs' not in littlefs['crunch_programs'] and
      'sbin/mount_ffs' in selected['crunch_programs'] and
      set(selected['crunch_programs']) - {'sbin/mount_ffs', 'sbin/fsck_ffs'} <=
      set(littlefs['crunch_programs']),
      'littlefs replaces only filesystem utilities and preserves network services')
flash_nodes = profile.network_device_selection(littlefs, 'flash0', '')
check('rflash0' in flash_nodes[0] and flash_nodes[3] == ('flash0',),
      'unpartitioned flash nodes use the generic flash driver numbering')
peripheral = profile.network('riscv32', ('system', 'audio', 'video', 'envsys', 'spi'),
                             ('1',))
transfer = profile.network('riscv32', ('transfer',))
spi_toolset = profile.network('riscv32', ('spi',))
all_network_toolsets = ('system', 'audio', 'video', 'envsys', 'spi',
                        'transfer', 'mdns', 'ssh', 'sftp', 'logging', 'display')
owner_profiles = (
    selected,
    profile.network('riscv32', all_network_toolsets, ('1',)),
    profile.network('riscv32', all_network_toolsets, ('1',),
                    root_filesystem='littlefs'))
for owner_profile in owner_profiles:
    crunch_owners = profile.network_crunch_owners(
        source, owner_profile['crunch_programs'])
    check(len(crunch_owners) == len(owner_profile['crunch_programs']) and
          all((source / owner).joinpath('Makefile').is_file()
              for owner in crunch_owners.values()),
          'selected network crunch owners resolve to actual OS Makefiles')
try:
    profile.network('riscv32', ('audio', 'audio'))
except ValueError:
    duplicate_toolset_rejected = True
else:
    duplicate_toolset_rejected = False
check(duplicate_toolset_rejected,
      'network toolset selector rejects duplicate selections')
try:
    profile.network('riscv32', ('spi',), ('1', '1'))
except ValueError:
    duplicate_spi_unit_rejected = True
else:
    duplicate_spi_unit_rejected = False
try:
    profile.network('riscv32', ('spi',), ('01',))
except ValueError:
    noncanonical_spi_unit_rejected = True
else:
    noncanonical_spi_unit_rejected = False
check(duplicate_spi_unit_rejected and noncanonical_spi_unit_rejected and
      not any(node.startswith('spi') for node in spi_toolset['device_nodes']) and
      not spi_toolset['device_make_groups'],
      'SPI node units require an explicit unique unit selection')
runtime_source = '/prepared/llvm-runtime-source'
check(profile.runtime_original_source({'paths': {'llvm-source': runtime_source}}) ==
      runtime_source and
      profile.runtime_original_source({'paths': {'llvm-source': runtime_source}},
                                      '/prepared/explicit-runtime-source') ==
      '/prepared/explicit-runtime-source',
      'runtime verification reuses the foundation-recorded source unless explicitly overridden')
with tempfile.TemporaryDirectory(prefix='network-modes-') as temporary:
    root = Path(temporary) / 'root'
    dest = Path(temporary) / 'dest'
    (root / 'usr/bin').mkdir(parents=True)
    (root / 'usr/libexec').mkdir(parents=True)
    (dest / 'usr/bin').mkdir(parents=True)
    (dest / 'usr/libexec').mkdir(parents=True)
    root_login = root / 'usr/bin/login'
    dest_login = dest / 'usr/bin/login'
    root_auth = root / 'usr/libexec/sshd-auth'
    dest_auth = dest / 'usr/libexec/sshd-auth'
    root_login.write_bytes(b'stripped login')
    dest_login.write_bytes(b'login executable')
    root_auth.write_bytes(b'stripped auth')
    dest_auth.write_bytes(b'auth executable')
    root_login.chmod(0o555)
    dest_login.chmod(0o555)
    root_auth.chmod(0o555)
    dest_auth.chmod(0o555)
    root_spec = Path(temporary) / 'root.spec'
    root_spec.write_text('/set type=file mode=0755\n'
                         '# ./usr/bin\nusr/bin type=dir\n    login mode=0555\n'
                         '# ./usr/libexec\nusr/libexec type=dir\n    sshd-auth\n')
    metalog = Path(temporary) / 'METALOG'
    metalog.write_text('./usr/bin/login type=file uname=root gname=wheel mode=04555 size=16\n'
                       './usr/bin/login type=file uname=root gname=wheel mode=04555 size=16\n'
                       './usr/libexec/sshd-auth type=file uname=root gname=wheel mode=0555 size=13\n')
    profile.apply_metalog_modes(root_spec, root, dest, metalog,
                                (('usr.bin/login', 'usr/bin/login'),
                                 ('sshd-auth', 'usr/libexec/sshd-auth')))
    check('mode=04555' in root_spec.read_text() and
          'sshd-auth mode=0555' in root_spec.read_text() and
          root_login.stat().st_mode & 0o7777 == 0o555 and
          dest_login.stat().st_mode & 0o7777 == 0o555 and
          root_auth.stat().st_mode & 0o7777 == 0o555 and
          dest_auth.stat().st_mode & 0o7777 == 0o555,
          'root spec expands inherited modes and restores setuid mode from METALOG without changing host copies')

with tempfile.TemporaryDirectory(prefix='network-rc-mode-') as temporary:
    temporary = Path(temporary)
    source_script = temporary / 'wpa_supplicant'
    staged_script = temporary / 'root/etc/rc.network.d/wpa_supplicant'
    source_script.write_text('#!/bin/sh\n')
    source_script.chmod(0o644)
    profile.stage_rc_script(source_script, staged_script)
    check(source_script.stat().st_mode & 0o777 == 0o644 and
          staged_script.stat().st_mode & 0o777 == 0o555,
          'rc staging applies target BINMODE when a host source script is non-executable')
recorded_foundation = {'usr/lib/libc.a': {'kind': 'file', 'sha256': 'a' * 64},
                       'usr/include/sys/types.h': {'kind': 'file', 'sha256': 'b' * 64}}
current_foundation = dict(recorded_foundation)
current_foundation.update({'usr/pkg': {'kind': 'directory'},
                           'usr/pkg/lib/libwolfssl.a': {'kind': 'file', 'sha256': 'c' * 64}})
check(profile.foundation_matches_receipt(current_foundation, recorded_foundation),
      'selected pkgsrc package additions are compatible with the original foundation receipt')
changed_foundation = dict(current_foundation)
changed_foundation['usr/lib/libc.a'] = {'kind': 'file', 'sha256': 'd' * 64}
check(not profile.foundation_matches_receipt(changed_foundation, recorded_foundation),
      'package allowance does not mask changes to recorded libc')
missing_foundation = dict(current_foundation)
missing_foundation.pop('usr/include/sys/types.h')
check(not profile.foundation_matches_receipt(missing_foundation, recorded_foundation),
      'package allowance does not mask missing recorded headers')
unexpected_foundation = dict(current_foundation)
unexpected_foundation['usr/lib/unrecorded.a'] = {'kind': 'file', 'sha256': 'e' * 64}
check(not profile.foundation_matches_receipt(unexpected_foundation, recorded_foundation),
      'only usr/pkg additions are permitted beside recorded foundation outputs')
check(selected['arch'] == 'riscv32' and selected['profile'] == 'network',
      'network selection uses the requested target')
with tempfile.TemporaryDirectory(prefix='network-logs-') as temporary:
    first_log = profile.network_orchestrator_log(temporary)
    second_log = profile.network_orchestrator_log(temporary)
    check(first_log.parent == second_log.parent and first_log != second_log and
          not first_log.exists() and not second_log.exists(),
          'incremental network runs receive unique append-preserving log paths')
check({'sbin/init', 'libexec/getty', 'external/bsd/dhcpcd/sbin/dhcpcd',
       'external/bsd/wpa/bin/wpa_supplicant', 'external/bsd/wpa/bin/wpa_cli',
       'usr.bin/getent', 'sbin/ping'} <=
      set(selected['crunch_programs']) and
      'external/apache2/mDNSResponder/usr.sbin/mdnsd' not in
      set(selected['crunch_programs']) and
      'external/apache2/mDNSResponder/usr.sbin/mdnsd' in
      set(mdns_toolset['crunch_programs']) and
      selected['programs'] == ('usr.bin/login',) and
      selected['install_map'] == (('usr.bin/login', 'usr/bin/login'),) and
      not selected['compact'] and all('SMALLPROG' not in flag for flag in selected['flags']),
      'network profile crunches getent/services/init/getty without SMALLPROG and keeps setuid tools separate')
check(set(selected['data_components']) == {
          'external/bsd/dhcpcd/libexec/dhcpcd-run-hooks',
          'external/bsd/dhcpcd/libexec/dhcpcd-hooks'} and
      all((source / part / 'Makefile').is_file() for part in selected['data_components']),
      'dhcpcd hooks are selected through native non-ELF install targets')
check(not any('SMALLPROG' in flag for flag in selected['flags']),
      'network target does not inherit compact-init flags')
def native_program_makefile(program):
    directory = source / program
    if not (directory / 'Makefile').is_file():
        directory = directory.parent
    return (directory / 'Makefile').is_file()


check(all(native_program_makefile(program) for program in
          (*selected['programs'], *selected['crunch_programs'])),
      'every selected native component has an OS make target')
check(all(native_program_makefile(program) for program in peripheral['crunch_programs']) and
      all((source / library / 'Makefile').is_file()
          for library in peripheral['libraries']),
      'selected peripheral toolsets have native program and library targets')
check('BINMODE=4555' in (source / 'sbin/ping/Makefile').read_text() and
      'LDADD+= -lipsec' in (source / 'sbin/ping/Makefile').read_text() and
      'lib/libipsec' in selected['libraries'] and
      'sbin/ping' in selected['crunch_programs'] and
      'sbin/ping' not in dict(selected['install_map']),
      'root-only ping uses normal socket ops in the 0555 crunch binary with IPsec support')
check('external/bsd/libpcap/lib' in selected['libraries'] and
      'lib/libpthread' in selected['libraries'] and
      'crypto/external/apache2/openssl/lib/libcrypto' in selected['libraries'] and
      'lib/libpthread' in profile.network('riscv32', wpa_backend='openssl')['libraries'] and
      'lib/libpthread' in profile.network('riscv32', wpa_backend='wolfssl')['libraries'],
      'OpenSSL and WolfSSL network crypto closures both select pthread')
check(all(path in transfer['crunch_programs'] for path in (
          'usr.bin/cksum/cksum', 'usr.bin/uuencode/uuencode',
          'usr.bin/uudecode/uudecode')) and
      not any(path in selected['crunch_programs'] for path in (
          'usr.bin/cksum/cksum', 'usr.bin/uuencode/uuencode',
          'usr.bin/uudecode/uudecode')),
      'text transfer utilities are optional and absent from the default network root')
check('lib/libz' not in selected['libraries'] and 'lib/libbz2' not in selected['libraries'],
      'compact network profile omits optional grep compression libraries')
crunch_source = (Path(__file__).with_name('cix-p1-build-userland.sh')).read_text()
peripheral_crunch_conf = profile.network_crunch_configuration(
    ' '.join(peripheral['crunch_programs']), 'wolfssl').splitlines()
peripheral_libs = next((line for line in peripheral_crunch_conf
                        if line.startswith('libs ')), '')
check('-ledit -lterminfo -lutil -lkvm -lprop -lcrypt -lpcap -lipsec -lm' in peripheral_libs and
      '-laudio' in peripheral_libs and '-lmj' in peripheral_libs and
      '-V EMBER_GREP_UNCOMPRESSED_ONLY=yes' in crunch_source,
      'network crunch includes service dependencies and compact grep option')
check('-lwolfssl' in peripheral_libs and '-lpthread' in peripheral_libs,
      'WolfSSL static consumers link the OS semaphore provider')
check('usr.sbin usr.bin/videoctl' in peripheral_crunch_conf[0] and
      any(line.startswith('special audioctl srcdir ') for line in peripheral_crunch_conf) and
      'obj/usr.bin/audio/common' in crunch_source and
      all(flag in crunch_source for flag in
          ('-V MKPAM=no', '-V USE_PAM=no', '-V MKKERBEROS=no',
           '-V USE_KERBEROS=no', '-V MKLDAP=no', '-V MKYP=no')) and
      'network_auth_flags' in crunch_source and
      '-v "$assignment" -V "$assignment"' in crunch_source and
      'EMBER_WPA_PROFILE=personal' in crunch_source and
      '"$wolfssl_prefix/lib"' in crunch_source,
      'crunchgen query/build receive selected network source and crypto settings')
check('LDFLAGS="${LDFLAGS:+$LDFLAGS }-Wl,--gc-sections"' not in crunch_source and
      "-V 'LDFLAGS.console-tools=-Wl,--gc-sections'" in crunch_source,
      'section GC applies to the final crunch executable, not program relocatables')
check('link_gc="LDFLAGS.${part##*/}=-Wl,--gc-sections"' in crunch_source and
      'component_make "$label-build" "$part" -j"$jobs" "$link_gc"' in crunch_source,
      'section GC reaches each standalone network executable through per-program link flags')
prog_makefile = (source / 'share/mk/bsd.prog.mk').read_text()
check('_LDFLAGS.${_P}=\t\t${LDFLAGS}  ${LDFLAGS.${_P}}' in prog_makefile and
      '${CC} ${LDFLAGS:N-pie} ${LD_DRIVER_FLAGS} -nostdlib -r -o ${.TARGET}' in
      prog_makefile,
      'per-program link flags do not affect the partial relocatable rule')
grep_makefile = (source / 'usr.bin/grep/Makefile').read_text()
check('${EMBER_GREP_UNCOMPRESSED_ONLY:Uno} == "yes"' in grep_makefile and
      'CPPFLAGS+=\t-DWITHOUT_GZIP -DWITHOUT_BZ2' in grep_makefile and
      'HOST_CPPFLAGS+=\t-DWITHOUT_GZIP -DWITHOUT_BZ2' in grep_makefile and
      'HOST_CPPFLAGS:M*-DWITHOUT_GZIP*' in grep_makefile and
      'HOST_CPPFLAGS:M*-DWITHOUT_BZ2*' in grep_makefile,
      'compact grep receives source and link options while normal grep stays unchanged')
check(selected['libraries'].index('lib/libterminfo') <
      selected['libraries'].index('lib/libedit'),
      'terminfo headers are staged before libedit dependency scanning')
with tempfile.TemporaryDirectory(prefix='network-reuse-') as temporary:
    output = Path(temporary) / 'output'
    compiler = {'family': 'clang', 'version': '23.0.0', 'triplet': 'riscv32--netbsd',
                'reported_triplet': 'riscv32-unknown-netbsd', 'resource_dir': '/host/clang'}
    runtime = {'format': 'emberbsd-llvm-runtime', 'version': 1,
               'compiler_family': 'clang', 'llvm_version': '23.0.0',
               'components': ['builtins', 'libunwind-static'],
               'target': 'riscv32', 'triplet': 'riscv32--netbsd',
               'machine_arch': 'riscv32', 'elf_machine': 243,
               'prefix': 'usr/pkg/llvm', 'source': {'sha512': 'ignored'}}
    compatibility = profile.network_build_compatibility(
        'riscv32', profile.network('riscv32'), {'arch': 'riscv32'}, compiler,
        runtime, 'wolfssl')
    check(not any(key in compatibility for key in
                  ('programs', 'libraries', 'overlay', 'source', 'worktree',
                   'openssh_crypto', 'syslogd_tls', 'syslogd_sign')) and
          compatibility['wpa_backend'] == 'wolfssl',
          'compatibility excludes optional service selections and source provenance')
    check(profile.network_output_action(output, compatibility, []) == 'new',
          'a missing network output starts a new build')
    output.mkdir()
    for name in ('dest', 'obj', 'routing', 'logs'):
        (output / name).mkdir()
    wrapper = output / 'nbmake-riscv'
    wrapper.write_text('# target args -m riscv -a riscv32 -O ' + str(output / 'obj') +
        ' -D ' + str(output / 'dest') + ' -T tools -V EXTERNAL_TOOLCHAIN=compiler'
        ' -V COPTS=-Oz -ffunction-sections -fdata-sections'
        ' -V LDFLAGS.console-tools=-Wl,--gc-sections')
    wrapper.chmod(0o755)
    (output / 'BUILD-INPUTS.txt').write_text('completed provenance receipt\n')
    wrapper_checks = ['-m riscv', '-a riscv32', '-O ' + str(output / 'obj'),
              '-D ' + str(output / 'dest'), '-T tools',
              '-V EXTERNAL_TOOLCHAIN=compiler']
    build_checks = ['-V COPTS=-Oz -ffunction-sections -fdata-sections',
                    '-V LDFLAGS.console-tools=-Wl,--gc-sections']
    check(profile.network_output_action(output, compatibility, wrapper_checks,
                                        build_checks) == 'reuse',
          'known compatible completed output is reusable without source or overlay identity gates')
    (output / 'NETWORK-COMPAT.json').write_text(
        __import__('json').dumps(compatibility, sort_keys=True) + '\n')
    check(profile.network_output_action(output, compatibility, wrapper_checks,
                                        build_checks) == 'reuse',
          'matching target, ABI, runtime, and compile configuration allows incremental reuse')
    candidate_libc = Path(temporary) / 'libc.a'
    staged_libc = output / 'dest/usr/lib/libc.a'
    staged_libc.parent.mkdir(parents=True)
    staged_libc.write_bytes(b'allocator A')
    candidate_libc.write_bytes(b'allocator A')
    check(profile.network_output_action(output, compatibility, wrapper_checks,
                                        foundation_libc=candidate_libc) == 'reuse',
          'a matching libc supports normal incremental reuse')
    candidate_libc.write_bytes(b'allocator B')
    try:
        profile.network_output_action(output, compatibility, wrapper_checks,
                                      foundation_libc=candidate_libc)
    except ValueError as error:
        check('different libc' in str(error), 'a changed allocator requires its corresponding output')
    else:
        raise AssertionError('reused objects with the wrong allocator')
    incompatible = dict(compatibility, arch='aarch64')
    try:
        profile.network_output_action(output, incompatible, wrapper_checks,
                                      build_checks)
    except ValueError:
        rejected_incompatible = True
    else:
        rejected_incompatible = False
    check(rejected_incompatible,
          'a genuinely incompatible target configuration is not reused')

    staging = profile.new_network_root(output)
    (staging / 'sbin').mkdir()
    (staging / 'sbin/init').write_text('new init')
    staging_spec = Path(str(staging) + '.spec')
    staging_spec.write_text('new device table\n')
    staged_receipt = output / 'BUILD-INPUTS.pending'
    staged_receipt.write_text('network-toolsets=\n')
    old_root = output / 'root'
    (old_root / 'sbin').mkdir(parents=True)
    (old_root / 'sbin/init').write_text('old init')
    (old_root / 'sbin/init').chmod(0o555)
    (old_root / 'stale-overlay-file').write_text('stale')
    (old_root / 'etc/rc.network.d').mkdir(parents=True)
    (old_root / 'etc/rc.network.d/mdnsd').write_text('old mdns service')
    (old_root / 'etc/rc.conf.d').mkdir(parents=True)
    (old_root / 'etc/rc.conf.d/mdnsd').write_text('mdnsd=YES\n')
    (old_root / 'bin').mkdir(parents=True)
    (old_root / 'bin/littlefs-vfs-check-memory-current').write_text('old test')
    old_spec = output / 'root.spec'
    old_spec.write_text('./etc/rc.network.d/mdnsd type=file\n'
                        './etc/rc.conf.d/mdnsd type=file\n'
                        './bin/littlefs-vfs-check-memory-current type=file\n')
    (output / 'BUILD-INPUTS.txt').write_text(
        'network-toolsets=system audio video envsys spi transfer\n'
        'program-sha256=previous-checker\n')
    profile.commit_network_root(staging, old_root, staging_spec, old_spec,
                                staged_receipt, output / 'BUILD-INPUTS.txt')
    check((old_root / 'sbin/init').read_text() == 'new init' and
          old_spec.read_text() == 'new device table\n' and
          (output / 'BUILD-INPUTS.txt').read_text() == 'network-toolsets=\n' and
          not staging.exists() and not staging_spec.exists(),
          'successful restaging commits root, device table, and receipt together')
    check(not (old_root / 'stale-overlay-file').exists(),
          'recreated root does not retain files removed from the selected overlay')
    check(not (old_root / 'etc/rc.network.d/mdnsd').exists() and
          not (old_root / 'etc/rc.conf.d/mdnsd').exists() and
          not (old_root / 'bin/littlefs-vfs-check-memory-current').exists() and
          'mdnsd' not in old_spec.read_text() and
          'littlefs-vfs-check' not in old_spec.read_text() and
          'program-sha256' not in (output / 'BUILD-INPUTS.txt').read_text(),
          'default recompose drops previous optional service, checker, manifest entries, and receipt inputs')

    link = Path(temporary) / 'root/bin/grep'
    link.parent.mkdir(parents=True)
    profile.link_if_same('console-tools', link)
    profile.link_if_same('console-tools', link)
    check(link.is_symlink() and os.readlink(link) == 'console-tools',
          'staging accepts an identical generated link on resume')
    try:
        profile.link_if_same('other', link)
    except ValueError:
        rejected_link_change = True
    else:
        rejected_link_change = False
    check(rejected_link_change,
          'staging rejects a generated link with a different target')
check((source / 'external/bsd/wpa/bin/Makefile.inc').is_file() and
      'EMBER_WPA_SOURCE' in
      (source / 'external/bsd/wpa/bin/Makefile.inc').read_text(),
      'WPA component requires its selected prepared source tree')
component_runner = (Path(__file__).with_name('cix-p1-build-userland.sh')).read_text()
check('wrapper_log=$out/logs/wrapper.log' in component_runner and
      'wrapper-network-$$.log' in component_runner and
      'while [ -e "$wrapper_log" ]' in component_runner and
      '--log "$wrapper_log"' in component_runner,
      'network retries preserve prior wrapper logs and select a fresh invocation log')
check('"EMBER_WPA_SOURCE=$wpa_source"' in component_runner and
      'EMBER_WPA_SOURCE=$wpa_source" -V' not in component_runner and
      'component_make "$label-includes" "$part" includes' in component_runner,
      'direct network make calls pass source assignments, including includes targets')
check("package == 'libpcap' and os.environ.get('EMBER_LIBPCAP_ARCHIVE')" in
      (Path(__file__).with_name('static-profile.py')).read_text() and
      "verify_command.extend(('--archive', str(archive)))" in
      (Path(__file__).with_name('static-profile.py')).read_text(),
      'libpcap archive is an optional payload-comparison input, not a generic network prerequisite')
profile_source = (Path(__file__).with_name('static-profile.py')).read_text()
check("package == 'wpa' and ('EMBER_WPA_ARCHIVE'" in profile_source and
      "'EMBER_WPA_HOSTAPD_ARCHIVE'" in profile_source and
      "'--hostapd-archive'" in profile_source and
      'WPA archive comparison requires both WPA and hostapd archives' in profile_source,
      'legacy WPA prepared receipts can be compared against both pinned upstream archives')
network_stage = component_runner.split('if [ "$profile" = network ]; then', 1)[1].split('exit 0', 1)[0]
console_crunch_branch = component_runner.split('if [ "$profile" = console-tools ]', 1)[1].split(
    'elif [ "$profile" = network ]', 1)[0]
network_crunch_branch = component_runner.split('elif [ "$profile" = network ]', 1)[1].split(
    'else\n', 1)[0]
check('for node in $device_block_nodes' in network_stage and
      'type=block' in network_stage and 'nbmakefs' not in network_stage,
      'network root stages the selected block device in its manifest without building an image')
check('component_make "$label-build" "$part" -j"$jobs" "$@" dependall' in component_runner and
      'component_make "$label-install" "$part" "$@" install' in component_runner,
      'direct obj, build and install calls share the network assignment path')
check('network-crunch-makeflags "$crunch_programs"' in component_runner and
      'network-crunch-config "$out/crunch/console-tools.conf"' in component_runner and
      '"MAKE=$HOST_SH $wrapper"' in console_crunch_branch and
      'network_auth_flags' not in console_crunch_branch and
      '"MAKE=$HOST_SH $wrapper $network_auth_flags"' in network_crunch_branch and
      '*" usr.sbin/syslogd/syslogd "*) logging_enabled=yes ;;' in component_runner,
      'the crunch producer consumes rendered link config and forwards selected make flags')
check(all((source / library / 'Makefile').is_file() for library in selected['libraries']),
      'network profile library closure has native OS make targets')
destinations = dict(selected['install_map'])
check(destinations == {'usr.bin/login': 'usr/bin/login'} and
      {'sbin/chown', 'sbin/fsck_ffs', 'libexec/getty',
       'external/bsd/dhcpcd/sbin/dhcpcd', 'external/bsd/wpa/bin/wpa_supplicant',
       'external/bsd/wpa/bin/wpa_cli',
       'external/apache2/mDNSResponder/usr.sbin/mdnsd'} <= set(mdns_toolset['crunch_programs']) and
      'sbin/ping' in selected['crunch_programs'] and
      'BINMODE=4555' in (source / 'usr.bin/login/Makefile').read_text(),
      'non-setuid services and root-only ping crunch while setuid login stays standalone')
check(not any(name in selected['programs'] for name in
              ('usr.bin/grep', 'usr.bin/sed', 'usr.bin/tee', 'usr.bin/tail')) and
      {'sbin/route', 'sbin/sysctl', 'usr.bin/grep', 'usr.bin/sed',
       'usr.bin/fmt'} <=
      set(selected['crunch_programs']),
      'rc, service and self-test utilities use the compact crunch binary')
check(not {'usr.bin/audio/ctl/audioctl', 'usr.bin/audio/play/audioplay',
           'usr.bin/audio/record/audiorecord', 'usr.bin/mixerctl/mixerctl',
           'usr.bin/timeout', 'usr.bin/wc', 'usr.bin/videoctl/videoctl',
           'usr.sbin/envstat/envstat', 'usr.sbin/spinandctl/spinandctl',
           'usr.bin/cksum/cksum', 'usr.bin/uuencode/uuencode',
           'usr.bin/uudecode/uudecode', 'bin/df', 'usr.bin/env', 'usr.bin/uname'} &
      set(selected['crunch_programs']) and
      {'usr.bin/audio/ctl/audioctl', 'usr.bin/audio/play/audioplay',
       'usr.bin/audio/record/audiorecord', 'usr.bin/mixerctl/mixerctl',
       'usr.bin/timeout', 'usr.bin/wc', 'usr.bin/videoctl/videoctl',
       'usr.sbin/envstat/envstat', 'usr.sbin/spinandctl/spinandctl'} <=
      set(peripheral['crunch_programs']) and
      {'usr.bin/audio/common', 'crypto/external/bsd/netpgp/libmj'} <=
      set(peripheral['libraries']),
      'optional audio/video/envsys/SPI tools share the compact network executable')
check(not {'bin/df', 'usr.bin/env', 'usr.bin/uname'} &
      set(selected['crunch_programs']) and
      {'bin/df', 'usr.bin/env', 'usr.bin/uname'} <=
      set(peripheral['crunch_programs']),
      'generic system diagnostics are opt-in and omitted from the minimal network profile')
check({'random', 'urandom', 'bpf'} <= set(selected['device_nodes']) and
      {'audio0', 'audioctl0', 'mixer0', 'video0', 'sysmon'} <=
      set(peripheral['device_nodes']) and {'spi1'} <= set(peripheral['device_nodes']) and
      {'bpf0'} <= set(selected['device_links']) and
      {'audio', 'audioctl', 'mixer'} <= set(peripheral['device_links']),
      'optional peripheral device nodes and links come from MAKEDEV')
network_devices = profile.network_device_selection(selected, 'md0')
peripheral_devices = profile.network_device_selection(peripheral, 'md0')
gui_network_devices = profile.network_device_selection(selected, 'md0', application=True)
check(network_devices[2] == ('bpf', 'md0') and
      'ptm' not in network_devices[2] and 'ptm' in ssh_nodes and
      profile.network_device_selection(ssh_toolset, 'md0')[2] ==
      ('bpf', 'md0', 'ptm') and
      peripheral_devices[2] == ('bpf', 'md0', 'audio', 'video', 'sysmon', 'spi1') and
      set(gui_network_devices[0]) >= {'ttyE0', 'wsmouse0', 'iic0'} and
      gui_network_devices[2] ==
      ('bpf', 'md0', 'wscons', 'iic0') and
      network_devices[0][-1] == 'rmd0a' and network_devices[3] == ('md0a',),
      'network root and optional GUI nodes come from selected MAKEDEV groups')
check('spi[0-9]*)' in (source / 'etc/MAKEDEV.tmpl').read_text() and
      'mkdev spi$unit c %spi_chr% $unit 600' in (source / 'etc/MAKEDEV.tmpl').read_text() and
      'device-major spi       char' in (source / 'sys/conf/majors').read_text(),
      'the optional SPI node uses the target native MAKEDEV and major definition')
try:
    profile.network_device_selection(selected, 'md0a')
except ValueError:
    valid_root_device = True
else:
    valid_root_device = False
named = profile.network_device_selection(selected, 'NAME=netbsd-root', '')
check(named[2] == ('bpf', 'dk') and not named[3] and
      profile.network_root_spec('NAME=netbsd-root', '') == 'NAME=netbsd-root',
      'a named GPT wedge uses the standard MAKEDEV wedge group and its filesystem label')
for label, part in [('NAME=', ''), ('NAME=bad node', ''), ('NAME=netbsd-root', 'a')]:
    try:
        profile.network_root_spec(label, part)
    except ValueError:
        continue
    raise SystemExit('FAIL: invalid network wedge selector accepted')
check(valid_root_device, 'root device input excludes an already-partitioned name')
check(set(profile.output_programs(selected)) ==
      {'bin/console-tools', *(destination for _, destination in selected['install_map'])},
      'ABI/linkage validation covers the crunch binary and every standalone target')
check('bin/lvgl-wscons' in profile.output_programs(selected, '/target/lvgl-wscons'),
      'an optional GUI/application target remains part of the network root checks')

native_path = Path(__file__).with_name('cix-p1-native.py')
native_spec = importlib.util.spec_from_file_location('cix_native_network', native_path)
native = importlib.util.module_from_spec(native_spec)
native_spec.loader.exec_module(native)
old_profile = os.environ.get('EMBER_USERLAND_PROFILE')
old_argv = __import__('sys').argv
required_inputs = ('EMBERBSD_SDK_SOURCE', 'EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE',
                   'EMBER_HOST_BFD_CONFIG', 'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE',
                   'EMBER_BYACC_SOURCE', 'EMBER_C_FOUNDATION', 'EMBER_LLVM_RUNTIME_STAGE',
                   'EMBER_CONSOLE_TARGET', 'EMBER_WPA_SOURCE', 'EMBER_PKGSRC_EXPORT',
                   'EMBER_NETWORK_ROOT_DEVICE')
saved_inputs = {name: os.environ.pop(name) for name in required_inputs if name in os.environ}
os.environ['EMBER_USERLAND_PROFILE'] = 'network'
__import__('sys').argv = ['cix-p1-native.py', '/src', '/out', '/tools', '/target']
try:
    try:
        native.main()
    except SystemExit as exc:
        dispatched = 'missing required inputs:' in str(exc)
    else:
        dispatched = False
finally:
    __import__('sys').argv = old_argv
    os.environ.update(saved_inputs)
    if old_profile is None:
        os.environ.pop('EMBER_USERLAND_PROFILE', None)
    else:
        os.environ['EMBER_USERLAND_PROFILE'] = old_profile
check(dispatched, 'native CLI dispatches network to its profile handler')

class Native:
    host = {}
    bundle_root = '/host-prefix'


names = []
profile.check_os_tools(Path('/host-tools'), Native(), '/selected/path',
                       lambda prefix, executable, host, search_path: names.append(Path(executable).name),
                       network=True)
check({'nbpwd_mkdb', 'nbcap_mkdb', 'nbmtree', 'nbcrunchgen', 'llvm-nm',
       'llvm-objcopy'} <= set(names) and 'nbmakefs' not in names,
      'network staging selects crunch tools and does not require image builder')

with tempfile.TemporaryDirectory(prefix='static-network-') as temporary:
    temporary = Path(temporary)
    overlay = temporary / 'overlay'
    (overlay / 'etc/rc.conf.d').mkdir(parents=True)
    (overlay / 'etc/myname').write_text('host-from-overlay\n')
    (overlay / 'etc/wpa_supplicant.conf').write_text('ctrl_interface=/var/run/wpa_supplicant\n')
    (overlay / 'etc/rc.conf.d/mdnsd').write_text('mdnsd_flags="-n host-from-overlay.local"\n')
    (overlay / 'etc/rc.conf.d/wpa_supplicant').write_text('wpa_supplicant=YES\n')
    (overlay / 'etc/ifconfig.wifi0').write_text('inet6 accept_rtadv\n')
    (overlay / 'etc/custom-network.conf').write_text('site-specific setting\n')
    check((overlay / 'etc/wpa_supplicant.conf').is_file(),
          'runtime credentials/configuration can live in the normal overlay')

    out = temporary / 'root'
    out.mkdir()
    bindir = temporary / 'tools'
    bindir.mkdir()
    pwd_mkdb = bindir / 'nbpwd_mkdb'
    pwd_mkdb.write_text(f'#!{sys.executable}\nfrom pathlib import Path\nimport sys\n'
                        'root = Path(sys.argv[4])\n'
                        "for name in ('passwd', 'pwd.db', 'spwd.db'):\n"
                        '    (root / "etc" / name).touch()\n')
    pwd_mkdb.chmod(0o755)
    cap_mkdb = bindir / 'nbcap_mkdb'
    cap_mkdb.write_text(f'#!{sys.executable}\nfrom pathlib import Path\nimport sys\n'
                        'Path(sys.argv[3] + ".db").touch()\n')
    cap_mkdb.chmod(0o755)
    dhcp_dest = temporary / 'dhcpcd-dest'
    (dhcp_dest / 'libexec/dhcpcd-hooks').mkdir(parents=True)
    (dhcp_dest / 'libexec/dhcpcd-run-hooks').write_text('#!/bin/sh\n')
    for hook in ('01-test', '20-resolv.conf', '30-hostname', '50-ntp.conf'):
        (dhcp_dest / 'libexec/dhcpcd-hooks' / hook).write_text('# generated ' + hook + '\n')
    profile.stage_network(out, source, overlay, 'wifi0', pwd_mkdb, dhcp_dest,
                          'md0', 'a', mdns_enabled=True)
    check(has_multiuser_rc_dependencies(out),
          'mDNS profile stages a closed multiuser rcorder provider chain')
    check((out / 'etc/rc.conf').read_text().find('rc_configured=YES') >= 0 and
          'wpa_supplicant=YES' in (out / 'etc/rc.conf').read_text() and
          'dhcpcd=YES' in (out / 'etc/rc.conf').read_text() and
          'mdnsd=YES' in (out / 'etc/rc.conf').read_text(),
          'generated rc enables standard WPA, DHCP and mDNS services')
    check('rc_status_display=""' in (out / 'etc/rc.conf').read_text() and
          'rc_status_display=""' in
          (source / 'etc/defaults/rc.conf').read_text(),
          'secondary startup display is disabled in common and generated defaults')
    check('constty "/usr/libexec/getty std.115200" vt100 on secure' in
          (out / 'etc/ttys').read_text(), 'standard getty preserves a login console')
    rc_conf = (out / 'etc/rc.conf').read_text()
    check('wpa_supplicant_flags="-Dbsd -i wifi0 -c /etc/wpa_supplicant.conf"' in rc_conf and
          'dhcpcd_flags="-b -C wpa_supplicant wifi0"' in rc_conf and
          'auto_ifconfig=NO' in rc_conf and 'net_interfaces="wifi0"' in rc_conf and
          'ifconfig_wifi0=' not in rc_conf and
          (out / 'etc/ifconfig.wifi0').read_text() == 'inet6 accept_rtadv\nup\n' and
          'hostname=' in rc_conf and 'ember-mosaico' not in rc_conf,
          'runtime interface uses the rc file mechanism and hostname remains overlay-selected')
    wedge_root = temporary / 'wedge-root'
    wedge_root.mkdir()
    profile.stage_network(wedge_root, source, overlay, 'wifi0', pwd_mkdb, dhcp_dest,
                          'NAME=netbsd-root', '')
    check((wedge_root / 'etc/fstab').read_text().splitlines()[0] == 'NAME=netbsd-root / ffs rw 1 1',
          'named root staging preserves the GPT label for mount and fsck')
    check('ROOT.a / ffs rw 1 1' in (out / 'etc/fstab').read_text() and
          '/tmp tmpfs rw' in (out / 'etc/fstab').read_text() and
          '/var/run tmpfs rw' in (out / 'etc/fstab').read_text() and
          'critical_filesystems_local="/tmp /var/run"' in (out / 'etc/rc.conf').read_text(),
          'temporary runtime filesystems mount through standard rc')
    check((out / 'etc/resolv.conf').is_file() and
          (out / 'var/db/dhcpcd').is_dir() and (out / 'var/run').is_dir() and
          (out / 'var/run/dhcpcd').is_dir() and
          (out / 'var/chroot/dhcpcd').is_dir() and (out / 'var/log').is_dir() and
          (out / 'var/tmp').is_dir(),
          'DHCP resolver, lease/state and runtime paths are writable locations')
    check((out / 'etc/rc.network.d/wpa_supplicant').is_file() and
          (out / 'etc/rc.network.d/wpa_supplicant').stat().st_mode & 0o777 == 0o555 and
          (out / 'etc/rc.network.d/mdnsd').is_file() and
          bool((out / 'etc/rc.network.d/network_tmpdirs').stat().st_mode & 0o111) and
          'BEFORE: wpa_supplicant dhcpcd mdnsd' in
          (out / 'etc/rc.network.d/network_tmpdirs').read_text() and
          (out / 'libexec/dhcpcd-run-hooks').is_file() and
          (out / 'libexec/dhcpcd-hooks/20-resolv.conf').is_file() and
          (out / 'etc/rc.conf.d/mdnsd').is_file(),
          'selected standard service scripts and DHCP hooks are staged')
    check(sum(line.startswith('_mdnsd:') for line in
              (out / 'etc/master.passwd').read_text().splitlines()) == 1 and
          sum(line.startswith('_dhcpcd:') for line in
              (out / 'etc/master.passwd').read_text().splitlines()) == 1 and
          sum(line.startswith('_dhcpcd:') for line in
              (out / 'etc/group').read_text().splitlines()) == 1 and
          (out / 'etc/group').is_file() and
          (out / 'etc/myname').read_text() == 'host-from-overlay\n' and
          'host-from-overlay.local' in (out / 'etc/rc.conf.d/mdnsd').read_text() and
          (out / 'etc/custom-network.conf').is_file(),
          'OS accounts remain default and overlay identity/service settings are staged')
    services = temporary / 'root-ssh-logging'
    services.mkdir()
    profile.stage_network(services, source, overlay, 'wifi0', pwd_mkdb, dhcp_dest,
                          'md0', 'a', mdns_enabled=True,
                          ssh_enabled=True, logging_enabled=True,
                          openssh_crypto='builtin')
    service_scripts = services / 'etc/rc.network.d'
    service_rc = (services / 'etc/rc.conf').read_text()
    service_provides, service_requires, service_missing = staged_rc_dependencies(services)
    sshd_config = (services / 'etc/ssh/sshd_config').read_text()
    check(not service_missing and
          {'sshd', 'sysdb', 'wscons', 'newsyslog', 'syslogd', 'network_logfs'} <=
          set(service_provides) and
          'LOGIN' in service_requires['sshd'] and
          set(service_requires['syslogd']) == {'network_logfs'} and
          set(service_requires['newsyslog']) == {'network_logfs'} and
          'wpa_supplicant mdnsd SERVERS' in
          (service_scripts / 'syslogd').read_text() and
          'syslogd SERVERS' in (service_scripts / 'newsyslog').read_text() and
          {'network', 'CRITLOCALMOUNTED'} <=
          set(service_requires['network_logfs']) and
          'newsyslog syslogd wpa_supplicant mdnsd' in
          (service_scripts / 'network_logfs').read_text(),
          'logging profile starts hostname, log storage and syslog before clients')
    check('syslogd_chroot_services=NO' in service_rc and
          not any('# KEYWORD: chrootdir' in path.read_text()
                  for path in service_scripts.iterdir() if path.is_file()),
          'closed network logging profile opts out of an empty chroot service scan')
    chroot_overlay = temporary / 'chroot-overlay'
    (chroot_overlay / 'etc/rc.network.d').mkdir(parents=True)
    (chroot_overlay / 'etc/rc.network.d/custom_chroot').write_text(
        '#!/bin/sh\n# ordinary comment \\\\\n# KEYWORD: chrootdir\n')
    (chroot_overlay / 'etc/rc.network.d/folded_chroot').write_text(
        '#!/bin/sh\n# KEYWOR\\\nD: chrootdir\n')
    chroot_root = temporary / 'root-chroot-overlay'
    chroot_root.mkdir()
    (chroot_root / 'dev').mkdir()
    profile.stage_network(chroot_root, source, chroot_overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', logging_enabled=True)
    check('syslogd_chroot_services=' not in
          (chroot_root / 'etc/rc.conf').read_text(),
          'logging profile retains automatic discovery when overlay adds a chroot service')
    ambiguous_overlay = temporary / 'ambiguous-keyword-overlay'
    (ambiguous_overlay / 'etc/rc.network.d').mkdir(parents=True)
    (ambiguous_overlay / 'etc/rc.network.d/continued_keyword').write_text(
        '#!/bin/sh\n# KEYWORD: network-appliance \\\n# harmless continuation\n')
    ambiguous_root = temporary / 'root-ambiguous-keyword'
    ambiguous_root.mkdir()
    (ambiguous_root / 'dev').mkdir()
    profile.stage_network(ambiguous_root, source, ambiguous_overlay, 'wifi0',
                          pwd_mkdb, dhcp_dest, 'md0', 'a', logging_enabled=True)
    check('syslogd_chroot_services=' not in
          (ambiguous_root / 'etc/rc.conf').read_text(),
          'profile stays conservative for a continued keyword without chrootdir')
    override_overlay = temporary / 'chroot-override-overlay'
    (override_overlay / 'etc/rc.extra.d').mkdir(parents=True)
    (override_overlay / 'etc/rc.extra.d/custom_chroot').write_text(
        '#!/bin/sh\n# PROVIDE: custom_chroot\n# KEYWORD: chrootdir\n')
    (override_overlay / 'etc/rc.conf.d').mkdir()
    (override_overlay / 'etc/rc.conf.d/syslogd').write_text(
        'rc_directories="/etc/rc.network.d /etc/rc.extra.d"\n')
    override_root = temporary / 'root-chroot-override'
    override_root.mkdir()
    (override_root / 'dev').mkdir()
    profile.stage_network(override_root, source, override_overlay, 'wifi0',
                          pwd_mkdb, dhcp_dest, 'md0', 'a', logging_enabled=True)
    check('syslogd_chroot_services=' not in
          (override_root / 'etc/rc.conf').read_text(),
          'syslogd keeps live discovery when rc.conf.d changes rc_directories')
    tagged_overlay = temporary / 'tagged-syslogd-overlay'
    (tagged_overlay / 'etc/rc.network.d').mkdir(parents=True)
    (tagged_overlay / 'etc/rc.network.d/syslogd').write_text(
        '#!/bin/sh\n# PROVIDE: syslogd\n# REQUIRE: network_logfs\n'
        '# BEFORE: wpa_supplicant SERVERS\n# KEYWORD: network-appliance\n'
        '# Existing syslogd comment mentions rcorder -k chrootdir.\n')
    tagged_root = temporary / 'root-tagged-syslogd'
    tagged_root.mkdir()
    (tagged_root / 'dev').mkdir()
    profile.stage_network(tagged_root, source, tagged_overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', logging_enabled=True)
    check('syslogd_chroot_services=NO' in
          (tagged_root / 'etc/rc.conf').read_text(),
          'ordinary keyword and body mention of chrootdir keeps the closed-profile opt-out')
    rcorder_path = os.environ.get('RCORDER_PATH')
    if rcorder_path:
        rc_scripts = sorted(service_scripts.iterdir())
        rc_result = subprocess.run(
            [rcorder_path, '-s', 'nostart', *map(str, rc_scripts)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False)
        check(rc_result.returncode == 0,
              'actual rcorder accepts the logging profile: ' + rc_result.stderr)
        rc_names = [Path(item).name for item in rc_result.stdout.splitlines()]
        rc_order = {name: index for index, name in enumerate(rc_names)}
        constraints = (('network', 'network_logfs'),
                       ('network_logfs', 'newsyslog'),
                       ('newsyslog', 'syslogd'),
                       ('syslogd', 'wpa_supplicant'),
                       ('syslogd', 'mdnsd'),
                       ('wpa_supplicant', 'NETWORKING'),
                       ('mdnsd', 'NETWORKING'),
                       ('NETWORKING', 'mountcritremote'))
        check(all(left in rc_order and right in rc_order and
                  rc_order[left] < rc_order[right]
                  for left, right in constraints),
              'actual rcorder starts network/hostname and local logging before WPA and mDNS')
    check('Subsystem sftp' not in sshd_config,
          'SSH command service does not advertise an uninstalled SFTP subsystem')
    check((services / 'dev/log').is_symlink() and
          __import__('os').readlink(services / 'dev/log') == '/var/run/log',
          'crunched syslogd keeps the standard /dev/log socket path')
    profile.stage_network(services, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', mdns_enabled=True,
                          ssh_enabled=True, logging_enabled=True,
                          openssh_crypto='builtin')
    check((services / 'dev/log').is_symlink() and
          __import__('os').readlink(services / 'dev/log') == '/var/run/log',
          'repeating network staging accepts the same syslog socket link')
    package = temporary / 'dropbear-prefix'
    for name, body in (('sbin/dropbear', 'test server'),
                       ('share/examples/rc.d/dropbear', '#!/bin/sh\n# PROVIDE: dropbear\n# REQUIRE: DAEMON\n')):
        entry = package / name
        entry.parent.mkdir(parents=True)
        entry.write_text(body)
    dbroot = temporary / 'dropbear-root'
    dbroot.mkdir()
    profile.stage_network(dbroot, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', ssh_enabled=True, dropbear_prefix=package)
    dbpolicy = (dbroot / 'etc/rc.conf.d/dropbear').read_text()
    check((dbroot / 'usr/pkg/sbin/dropbear').read_bytes() == (package / 'sbin/dropbear').read_bytes()
          and (dbroot / 'etc/rc.network.d/dropbear').stat().st_mode & 0o111
          and (dbroot / 'dev/pts').is_dir()
          and 'dropbear=YES' in (dbroot / 'etc/rc.conf').read_text()
          and not (dbroot / 'etc/rc.network.d/sshd').exists()
          and not (dbroot / 'etc/ssh/sshd_config').exists()
          and '-s -j -k' in dbpolicy and '-R' not in dbpolicy
          and 'required_files="/etc/dropbear/dropbear_ed25519_host_key"' in dbpolicy,
          'the packaged daemon uses a provisioned host key and public-key authentication')
    sftp_services = temporary / 'root-ssh-sftp'
    sftp_services.mkdir()
    profile.stage_network(sftp_services, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', mdns_enabled=True,
                          ssh_enabled=True, logging_enabled=True,
                          openssh_crypto='builtin', sftp_enabled=True)
    check('Subsystem sftp /usr/libexec/sftp-server' in
          (sftp_services / 'etc/ssh/sshd_config').read_text(),
          'selected SFTP service advertises its staged subsystem')
    syslogd_source = (source / 'etc/rc.d/syslogd').read_text()
    check('for _rd in ${rc_directories:-/etc/rc.d}; do' in syslogd_source and
          'rcorder -k chrootdir "${_rd}"/*' in syslogd_source and
          'rcorder -k chrootdir /etc/rc.d/*' not in syslogd_source,
          'syslogd retains automatic discovery for dynamic rc directory configurations')
    check('sshd=YES' in (services / 'etc/rc.conf').read_text() and
          'syslogd=YES' in (services / 'etc/rc.conf').read_text() and
          'syslogd_flags="-s"' in (services / 'etc/rc.conf').read_text() and
          'newsyslog=YES' in (services / 'etc/rc.conf').read_text() and
          'network_logfs_size=1m' in (services / 'etc/rc.conf').read_text() and
          'sshd_key_types="ed25519"' in (services / 'etc/rc.conf').read_text() and
          'wpa_supplicant_flags="-s -Dbsd -i wifi0 -c /etc/wpa_supplicant.conf"' in
          (services / 'etc/rc.conf').read_text() and
          'sysdb=NO' in (services / 'etc/rc.conf').read_text() and
          'wscons=NO' in (services / 'etc/rc.conf').read_text() and
          'critical_filesystems_local="/tmp /var/run /dev/pts"' in
          (services / 'etc/rc.conf').read_text() and
          'ptyfs /dev/pts ptyfs rw' in (services / 'etc/fstab').read_text() and
          (services / 'dev/pts').is_dir() and
          (services / 'etc/rc.network.d/sshd').is_file() and
          (services / 'etc/rc.network.d/syslogd').is_file() and
          (services / 'etc/rc.network.d/newsyslog').is_file(),
          'selected SSH and logging startup is enabled while unrelated rc providers stay disabled')
    log_mount_text = (service_scripts / 'network_logfs').read_text()
    newsyslog_conf = (services / 'etc/newsyslog.conf').read_text()
    syslog_conf = (services / 'etc/syslog.conf').read_text()
    check('tmpfs /var/log' not in (services / 'etc/fstab').read_text() and
          'network_logfs_size:=1m' in log_mount_text and
          '-s "$network_logfs_size"' in log_mount_text and
          ': >> /var/log/messages\n' in log_mount_text and
          ': >> /var/log/authlog\n' in log_mount_text,
          'logging creates a capped volatile log filesystem without truncating existing logs')
    check('/var/log/messages 0644 3 64 * -' in newsyslog_conf and
          '/var/log/authlog 0600 3 64 * -' in newsyslog_conf and
          '*.info;auth,authpriv.none /var/log/messages' in syslog_conf and
          'auth,authpriv.info /var/log/authlog' in syslog_conf and
          not syslog_has_forward_action(syslog_conf) and
          syslog_has_forward_action('*.info @logserver\n') and
          syslog_has_forward_action('*.info @[logserver]\n') and
          not (services / 'etc/services').exists() and
          (services / 'etc/syslog.conf').is_file() and
          (services / 'etc/newsyslog.conf').is_file(),
          'forwarding detection reads action fields; local-only logging needs no services file')
    wpa_logging = (not wpa_selected or
          ('-DCONFIG_DEBUG_SYSLOG' in
          (source / 'external/bsd/wpa/bin/wpa_supplicant/Makefile').read_text() and
          '  -s = log output to syslog instead of stdout' in
          (wpa_source / 'wpa_supplicant/main.c').read_text()))
    check(wpa_logging and
          'tmpfs /tmp tmpfs rw' in (services / 'etc/fstab').read_text() and
          'tmpfs /var/run tmpfs rw' in (services / 'etc/fstab').read_text(),
          'selected wpa_supplicant supports routing normal and debug logs to syslog')
    check('PermitRootLogin prohibit-password' in sshd_config and
          'HostKey /etc/ssh/ssh_host_ed25519_key' in sshd_config and
          'PasswordAuthentication no' in sshd_config and
          'KbdInteractiveAuthentication no' in sshd_config and
          'AuthenticationMethods publickey' in sshd_config and
          'UsePAM' not in sshd_config and
          'Subsystem sftp' not in sshd_config and
          (services / 'var/chroot/sshd').is_dir() and
          (services / 'etc/master.passwd').read_text().find('sshd:*:16:16:') >= 0,
          'stock SSH staging is key-only without SFTP and has the OS privsep account path')
    openssl_services = temporary / 'root-ssh-openssl'
    openssl_services.mkdir()
    profile.stage_network(openssl_services, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', ssh_enabled=True)
    check('sshd_key_types="ecdsa ed25519 rsa"' in
          (openssl_services / 'etc/rc.conf').read_text() and
          'HostKey /etc/ssh/ssh_host_ed25519_key' not in
          (openssl_services / 'etc/ssh/sshd_config').read_text(),
          'OpenSSL-mode SSH retains the generic host-key algorithm selection')
    cli_minimal = temporary / 'root-cli-minimal'
    cli_minimal.mkdir()
    cli_result = subprocess.run(
        [sys.executable, '-B', str(path), 'stage-network', str(cli_minimal),
         str(source), str(overlay), 'wifi0', str(pwd_mkdb), str(dhcp_dest),
         'md0', 'a', 'ffs', 'no', 'no', 'no', 'builtin', 'no', 'no'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    check(cli_result.returncode == 0 and
          'sshd=NO' in (cli_minimal / 'etc/rc.conf').read_text(),
          'network staging CLI accepts the resolved no-SSH builtin-mode default')
    display_root = temporary / 'root-display'
    display_root.mkdir()
    profile.stage_network(display_root, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a', display_enabled=True)
    display_rc = (display_root / 'etc/rc.conf').read_text()
    check('rc_status_display="/dev/ttyE0"' in display_rc and
          'wscons=' not in display_rc and
          not (display_root / 'etc/rc.network.d/wscons').exists(),
          'display staging selects only a secondary sink without wscons startup')
    check(not (services / 'etc/ssh/ssh_host_ed25519_key').exists() and
          not (services / 'root/.ssh/authorized_keys').exists() and
          not (services / 'etc/rc.d').exists() and
          (services / 'etc/rc.network.d/network_logfs').stat().st_mode & 0o777 == 0o555,
          'host keys and user authorized keys stay out of the generic image')
    minimal = temporary / 'root-minimal-network'
    minimal.mkdir()
    profile.stage_network(minimal, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'md0', 'a')
    check(has_multiuser_rc_dependencies(minimal),
          'normal profile stages a closed multiuser rcorder provider chain')
    minimal_rc = (minimal / 'etc/rc.network.d/network_tmpdirs').read_text()
    check(not (minimal / 'etc/rc.network.d/mdnsd').exists() and
          not (minimal / 'etc/rc.network.d/sshd').exists() and
          not (minimal / 'etc/rc.network.d/syslogd').exists() and
          not (minimal / 'etc/rc.network.d/network_logfs').exists() and
          not (minimal / 'etc/ssh/sshd_config').exists() and
          not (minimal / 'etc/syslog.conf').exists() and
          not (minimal / 'etc/rc.conf.d/mdnsd').exists() and
          'mdnsd=NO' in (minimal / 'etc/rc.conf').read_text() and
          'sshd=NO' in (minimal / 'etc/rc.conf').read_text() and
          'syslogd=NO' in (minimal / 'etc/rc.conf').read_text() and
          'syslogd_flags=' not in (minimal / 'etc/rc.conf').read_text() and
          'newsyslog=NO' in (minimal / 'etc/rc.conf').read_text() and
          'rc_status_display=""' in (minimal / 'etc/rc.conf').read_text() and
          '/dev/pts' not in (minimal / 'etc/fstab').read_text() and
          'ptm' not in (minimal / 'etc/rc.conf').read_text() and
          'sysdb=NO' not in (minimal / 'etc/rc.conf').read_text() and
          'wscons=NO' not in (minimal / 'etc/rc.conf').read_text() and
          'wpa_supplicant_flags="-Dbsd -i wifi0 -c /etc/wpa_supplicant.conf"' in
          (minimal / 'etc/rc.conf').read_text() and
          not (minimal / 'etc/rc.d').exists() and
          'BEFORE: wpa_supplicant dhcpcd' in minimal_rc and
          'mdnsd' not in minimal_rc,
          'default network staging omits optional mDNS service and overlay override')
    check((minimal / 'etc/rc.network.d/wpa_supplicant').read_text() ==
          (source / 'etc/rc.d/wpa_supplicant').read_text(),
          'profiles without logging retain the standard WPA rc dependency')
    (overlay / 'etc/custom-network.conf').write_text('updated setting\n')
    profile.stage_network(out, source, overlay, 'wifi0', pwd_mkdb, dhcp_dest, 'md0', 'a')
    check(sum(line.startswith('_mdnsd:') for line in
              (out / 'etc/master.passwd').read_text().splitlines()) == 1 and
          (out / 'etc/wpa_supplicant.conf').is_file() and
          (out / 'etc/ifconfig.wifi0').read_text() == 'inet6 accept_rtadv\nup\n' and
          (out / 'etc/custom-network.conf').read_text() == 'updated setting\n',
          'network staging accepts updated overlay files without duplicate accounts or interface settings')

    dotted = temporary / 'root-dotted'
    dotted.mkdir()
    profile.stage_network(dotted, source, overlay, 'wifi0.1', pwd_mkdb, dhcp_dest, 'md0', 'a')
    check('wifi0.1' in (dotted / 'etc/rc.conf').read_text() and
          (dotted / 'etc/ifconfig.wifi0.1').read_text() == 'up\n' and
          'ifconfig_wifi0.1=' not in (dotted / 'etc/rc.conf').read_text(),
          'dot-containing interface names use a valid rc file, not shell variable syntax')

    flash_root = temporary / 'root-littlefs'
    flash_root.mkdir()
    profile.stage_network(flash_root, source, overlay, 'wifi0', pwd_mkdb,
                          dhcp_dest, 'flash0', '', 'littlefs')
    check((flash_root / 'etc/fstab').read_text().splitlines() == [
              '/dev/flash0 / littlefs rw 0 0',
              'tmpfs /tmp tmpfs rw', 'tmpfs /var/run tmpfs rw'],
          'littlefs root uses its flash node and upstream recovery rather than an FFS checker')

    try:
        profile.stage_overlay(overlay, out)
    except ValueError:
        rejected = True
    else:
        rejected = False
    check(rejected, 'existing overlay contract rejects conflicts with base paths')

print(f'static network profile: {checks} checks, 0 failures')
