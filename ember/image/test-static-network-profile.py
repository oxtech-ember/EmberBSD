#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise the network profile selector and standard-root staging contract."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile


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


selected = profile.network('riscv32')
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
    (dest / 'usr/bin').mkdir(parents=True)
    root_login = root / 'usr/bin/login'
    dest_login = dest / 'usr/bin/login'
    root_login.write_bytes(b'stripped login')
    dest_login.write_bytes(b'login executable')
    root_login.chmod(0o555)
    dest_login.chmod(0o555)
    root_spec = Path(temporary) / 'root.spec'
    root_spec.write_text('# ./usr/bin\nusr/bin type=dir\n    login mode=0555\n# ./usr/bin\n..\n')
    metalog = Path(temporary) / 'METALOG'
    metalog.write_text('./usr/bin/login type=file uname=root gname=wheel mode=04555 size=16\n'
                       './usr/bin/login type=file uname=root gname=wheel mode=04555 size=16\n')
    profile.apply_metalog_modes(root_spec, root, dest, metalog,
                                (('usr.bin/login', 'usr/bin/login'),))
    check('mode=04555' in root_spec.read_text() and
          root_login.stat().st_mode & 0o7777 == 0o555 and
          dest_login.stat().st_mode & 0o7777 == 0o555,
          'root spec restores target setuid mode from METALOG without changing host copies')
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
       'external/apache2/mDNSResponder/usr.sbin/mdnsd', 'usr.bin/getent',
       'sbin/ping'} <=
      set(selected['crunch_programs']) and
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
check(all((source / program / 'Makefile').is_file() for program in
          (*selected['programs'], *selected['crunch_programs'])),
      'every selected native component has an OS make target')
check('BINMODE=4555' in (source / 'sbin/ping/Makefile').read_text() and
      'LDADD+= -lipsec' in (source / 'sbin/ping/Makefile').read_text() and
      'lib/libipsec' in selected['libraries'] and
      'sbin/ping' in selected['crunch_programs'] and
      'sbin/ping' not in dict(selected['install_map']),
      'root-only ping uses normal socket ops in the 0555 crunch binary with IPsec support')
check('external/bsd/libpcap/lib' in selected['libraries'],
      'selected WPA link dependency libpcap is built by its OS target')
check('lib/libz' not in selected['libraries'] and 'lib/libbz2' not in selected['libraries'],
      'compact network profile omits optional grep compression libraries')
crunch_source = (Path(__file__).with_name('cix-p1-build-userland.sh')).read_text()
check("printf 'libs -ledit -lterminfo -lutil -lkvm -lprop -lcrypt -lpcap -lipsec -lm'" in crunch_source and
      '-V EMBER_GREP_UNCOMPRESSED_ONLY=yes' in crunch_source,
      'network crunch includes service dependencies and compact grep option')
check("printf 'srcdirs bin sbin usr.bin libexec external/bsd/dhcpcd/sbin external/bsd/wpa/bin external/apache2/mDNSResponder/usr.sbin\\nprogs'" in crunch_source and
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
        'riscv32', selected, {'arch': 'riscv32'}, compiler, runtime, 'wolfssl')
    check(not any(key in compatibility for key in
                  ('programs', 'libraries', 'overlay', 'source', 'worktree')) and
          compatibility['wpa_backend'] == 'wolfssl',
          'compatibility is independent of source provenance, app selection and overlays')
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
    staged_receipt.write_text('new provenance\n')
    old_root = output / 'root'
    (old_root / 'sbin').mkdir(parents=True)
    (old_root / 'sbin/init').write_text('old init')
    (old_root / 'sbin/init').chmod(0o555)
    (old_root / 'stale-overlay-file').write_text('stale')
    old_spec = output / 'root.spec'
    old_spec.write_text('old device table\n')
    profile.commit_network_root(staging, old_root, staging_spec, old_spec,
                                staged_receipt, output / 'BUILD-INPUTS.txt')
    check((old_root / 'sbin/init').read_text() == 'new init' and
          old_spec.read_text() == 'new device table\n' and
          (output / 'BUILD-INPUTS.txt').read_text() == 'new provenance\n' and
          not staging.exists() and not staging_spec.exists(),
          'successful restaging commits root, device table, and receipt together')
    check(not (old_root / 'stale-overlay-file').exists(),
          'recreated root does not retain files removed from the selected overlay')

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
check('for node in $device_block_nodes' in network_stage and
      'type=block' in network_stage and 'nbmakefs' not in network_stage,
      'network root stages the selected block device in its manifest without building an image')
check('component_make "$label-build" "$part" -j"$jobs" "$@" dependall' in component_runner and
      'component_make "$label-install" "$part" "$@" install' in component_runner,
      'direct obj, build and install calls share the network assignment path')
check(all((source / library / 'Makefile').is_file() for library in selected['libraries']),
      'network profile library closure has native OS make targets')
destinations = dict(selected['install_map'])
check(destinations == {'usr.bin/login': 'usr/bin/login'} and
      {'sbin/chown', 'sbin/fsck_ffs', 'libexec/getty',
       'external/bsd/dhcpcd/sbin/dhcpcd', 'external/bsd/wpa/bin/wpa_supplicant',
       'external/bsd/wpa/bin/wpa_cli',
       'external/apache2/mDNSResponder/usr.sbin/mdnsd'} <= set(selected['crunch_programs']) and
      'sbin/ping' in selected['crunch_programs'] and
      'BINMODE=4555' in (source / 'usr.bin/login/Makefile').read_text(),
      'non-setuid services and root-only ping crunch while setuid login stays standalone')
check(not any(name in selected['programs'] for name in
              ('usr.bin/grep', 'usr.bin/sed', 'usr.bin/tee', 'usr.bin/tail')) and
      {'sbin/route', 'usr.bin/grep', 'usr.bin/sed', 'usr.bin/fmt'} <=
      set(selected['crunch_programs']),
      'rc and service utilities use the compact crunch binary')
check({'random', 'urandom', 'bpf'} <= set(selected['device_nodes']) and
      'bpf0' in selected['device_links'],
      'network root includes entropy and BPF device nodes from MAKEDEV')
network_devices = profile.network_device_selection(selected, 'md0')
gui_network_devices = profile.network_device_selection(selected, 'md0', application=True)
check(network_devices[2] == ('bpf', 'md0') and
      set(gui_network_devices[0]) >= {'ttyE0', 'wsmouse0', 'iic0'} and
      gui_network_devices[2] ==
      ('bpf', 'md0', 'wscons', 'iic0') and
      network_devices[0][-1] == 'rmd0a' and network_devices[3] == ('md0a',),
      'network root and optional GUI nodes come from selected MAKEDEV groups')
try:
    profile.network_device_selection(selected, 'md0a')
except ValueError:
    valid_root_device = True
else:
    valid_root_device = False
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
    profile.stage_network(out, source, overlay, 'wifi0', pwd_mkdb, dhcp_dest, 'md0', 'a')
    check((out / 'etc/rc.conf').read_text().find('rc_configured=YES') >= 0 and
          'wpa_supplicant=YES' in (out / 'etc/rc.conf').read_text() and
          'dhcpcd=YES' in (out / 'etc/rc.conf').read_text() and
          'mdnsd=YES' in (out / 'etc/rc.conf').read_text(),
          'generated rc enables standard WPA, DHCP and mDNS services')
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
    check((source / 'ember/image/static-profile.py').read_text().find(
          "len(sys.argv) == 10 and sys.argv[1] == 'stage-network'") >= 0,
          'standard staged network command accepts selected root device and partition')
    check((out / 'etc/rc.network.d/wpa_supplicant').is_file() and
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

    try:
        profile.stage_overlay(overlay, out)
    except ValueError:
        rejected = True
    else:
        rejected = False
    check(rejected, 'existing overlay contract rejects conflicts with base paths')

print(f'static network profile: {checks} checks, 0 failures')
