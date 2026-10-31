#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise native reduced-userland orchestration against synthetic build tools."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

here = Path(__file__).parent
builder = here / 'cix-p1-build-userland.sh'
assert builder.is_file(), 'reduced userland builder is missing'
prefix = Path(os.environ['EMBER_TEST_NATIVE_PREFIX'])
shell = prefix / 'bin/mksh'
python = prefix / 'bin/python3.15'
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def write(path, data, mode=None):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data)
    if mode is not None:
        path.chmod(mode)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix='ember-cix-userland-') as name:
    work = Path(name)
    src, tools, compiler, libedit = (work / n for n in ('src', 'tools', 'compiler', 'libedit'))
    for directory in (src, tools / 'bin', compiler / 'bin', libedit):
        directory.mkdir(parents=True)
    write(libedit / 'libedit-source.json', '{}\n')
    write(libedit / 'ports.distinfo', 'synthetic\n')
    write(compiler / 'libgcc.a', 'retained compiler runtime')
    write(compiler / 'libgcc_eh.a', 'retained compiler unwinder')
    write(compiler / 'bin/aarch64--netbsd-gcc', f'''#!{shell}
case "$1" in
-dumpfullversion) echo "${{TEST_GCC_VERSION:-16.2.0}}";;
-dumpmachine) echo aarch64--netbsd;;
-print-libgcc-file-name) echo '{compiler}/libgcc.a';;
-print-file-name=libgcc_eh.a)
  if [ "$TEST_NO_UNWINDER" = 1 ]; then echo libgcc_eh.a; else echo '{compiler}/libgcc_eh.a'; fi;;
-print-file-name=include) echo '{compiler}/include';;
-print-file-name=include-fixed) echo '{compiler}/include-fixed';;
*) exit 2;;
esac
''', 0o755)
    readelf = f'''#!{shell}
test -s "$2" || exit 1
if [ -n "$TEST_LATE_PROFILE_FILE" ]; then printf '\\n# late fixture change\\n' >> "$TEST_LATE_PROFILE_FILE"; fi
if [ -n "$TEST_LATE_DATA_FILE" ]; then printf 'late change\\n' >> "$TEST_LATE_DATA_FILE"; fi
test "$TEST_ELF_KIND" != invalid || exit 1
echo 'Class: ELF64'
if [ "$TEST_ELF_KIND" = big-endian ]; then echo "Data: 2's complement, big endian"; else echo "Data: 2's complement, little endian"; fi
echo 'Machine: AArch64'
echo 'Type: EXEC (Executable file)'
if [ "$TEST_ELF_KIND" = dynamic ] && [ "$1" = -l ]; then echo INTERP; fi
'''
    for tool in ('aarch64--netbsd-readelf', 'llvm-readelf'):
        write(compiler / 'bin' / tool, readelf, 0o755)
    write(compiler / 'bin/clang', f'#!{shell}\nexit 0\n', 0o755)
    for package in ('libarchive', 'libevent', 'byacc', 'iscsi', 'libpcap', 'tcpwrappers', 'libcbor', 'libfido2'):
        write(work / f'{package}-source/emberbsd-prepared-source.json', f'{package}-source')
    write(work / 'llvm-source/llvm-source.json', 'prepared LLVM source')
    write(work / 'llvm-resource/lib/netbsd/builtins.a', 'retained LLVM builtins')
    for tool in ('nbmkesdb', 'nbmkcsmapper'):
        write(tools / 'bin' / tool, f'#!{shell}\nexit 0\n', 0o755)
    build = (here / 'fixtures/cix-userland-build.sh').read_text().replace('@SHELL@', str(shell))
    write(src / 'build.sh', build, 0o755)
    git = os.environ.get('EMBER_GIT', '/usr/bin/git')
    git_env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_NOSYSTEM='1')
    for args in (['init', '-q'], ['add', 'build.sh'],
                 ['-c', 'user.name=Contract', '-c', 'user.email=contract@example.invalid',
                  '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'fixture']):
        subprocess.run([git, '-C', str(src), *args], check=True, env=git_env)
    routing = work / 'routing'
    routing.mkdir()
    (routing / 'awk').symlink_to(prefix / 'bin/nawk')
    role_log = work / 'role-calls'
    routes = {}
    for role, real in (('git', git), ('python', str(python)), ('shasum', str(prefix / 'bin/shasum'))):
        route = work / ('selected-' + role)
        write(route, f'#!{shell}\nprintf \'{role}\\n\' >> \'{role_log}\'\nexec \'{real}\' "$@"\n', 0o755)
        routes['TEST_' + role.upper()] = str(route)
    sdk = work / 'sdk'
    write(sdk / 'scripts/run-native-os.py', '''import os, subprocess, sys
args = sys.argv[1:]
source = args[args.index('--source') + 1]
build_args = args[args.index('--') + 1:]
sys.exit(subprocess.call([os.environ['HOST_SH'], source + '/build.sh', *build_args]))
''')

    def selected_runner(script, output, extra):
        env = dict(os.environ, HOST_SH=str(shell), PATH=f'{routing}:{prefix}/bin:{prefix}/gnu/bin', **extra)
        args = [str(shell), str(script), '--selected', str(src), str(output), str(tools), str(compiler),
                str(sdk), str(prefix), 'clang', extra.get('TEST_FAMILY', 'gcc'),
                extra.get('EMBER_LIBEDIT_SOURCE', str(libedit)), str(work / 'llvm-source'),
                str(work / 'host-bfd.mk'), '2', extra.get('TEST_GIT', git),
                extra.get('TEST_PYTHON', str(python)), extra.get('TEST_SHASUM', str(prefix / 'bin/shasum')),
                *(str(work / f'{p}-source') for p in ('libarchive', 'libevent', 'byacc', 'iscsi', 'libpcap', 'tcpwrappers', 'libcbor', 'libfido2')),
                str(work / 'llvm-runtime'), str(work / 'llvm-resource'), str(compiler / 'libgcc_eh.a'),
                str(compiler / 'resource'), '23.1.4', 'aarch64-unknown-netbsd',
                str(work / 'llvm-resource/lib/netbsd/builtins.a')]
        return subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def run(output, **extra):
        inputs = dict(EMBER_LIBEDIT_SOURCE=str(libedit), TEST_FAIL_COMPONENT='', TEST_OMIT_BINARY='',
                      TEST_ELF_KIND='', TEST_FAIL_MKNOD_STAGE='', TEST_OMIT_DATA='', TEST_BAD_DATA='',
                      TEST_LATE_PROFILE_FILE='', TEST_LATE_DATA_FILE='')
        inputs.update(routes)
        inputs.update(extra)
        return selected_runner(builder, output, inputs)

    out = work / 'accepted'
    result = run(out)
    check(result.returncode == 0, f'valid profile: {result.stdout}{result.stderr}')
    clang_out = work / 'clang-accepted'
    result = run(clang_out, TEST_FAMILY='clang')
    check(result.returncode == 0, f'Clang profile: {result.stdout}{result.stderr}')
    clang_flags = (clang_out / 'build-flags').read_text()
    for flag in ('EMBER_COMPILER_FAMILY=clang', 'EMBER_LLVM_RUNTIME_PHASE=bootstrap', 'MKLIBCXX=no', 'MKCOMPAT=no'):
        check(flag in clang_flags, f'missing Clang flag {flag}')
    for package in ('LIBARCHIVE', 'LIBEVENT', 'BYACC'):
        check(f'EMBER_{package}_SOURCE={work}/{package.lower()}-source' in clang_flags, f'{package} source not forwarded')
    check(f'EMBER_LLVM_RESOURCE_DIR={work}/llvm-resource' in clang_flags, 'selected resource not forwarded')
    events = (out / 'events').read_text().splitlines(keepends=True)
    dhcpcd = [e for e in events if '/external/bsd/dhcpcd/sbin/dhcpcd ' in e]
    check(len(dhcpcd) == 3 and [e.split()[-1] for e in dhcpcd] == ['obj', 'dependall', 'install'] and
          all(' SMALLPROG=1 SMALLPROG_INET6=1 ' in e for e in dhcpcd), 'reduced DHCP retains IPv4 and IPv6')
    for part, selection in (('iconv', ''), ('esdb', 'SUBDIR=ISO646 UTF ISO646_PART=US UTF_PART=8 '),
                            ('csmapper', 'SUBDIR=ISO646 PART_ISO646=US ')):
        selected = [e for e in events if f'/share/i18n/{part} ' in e]
        check(len(selected) == 3 and all(selected[i].endswith(selection + stage + '\n')
              for i, stage in enumerate(('obj', 'dependall', 'install'))), f'native DATA selection {part}')
    ssh_parts = '''external/bsd/blocklist/lib lib/libwrap crypto/external/bsd/openssh/lib
      crypto/external/bsd/openssh/bin/sshd crypto/external/bsd/openssh/libexec/sshd-session
      crypto/external/bsd/openssh/libexec/sshd-auth crypto/external/bsd/openssh/bin/ssh-keygen
      usr.bin/passwd usr.sbin/pwd_mkdb sbin/mount_ptyfs'''.split()
    for part in ssh_parts:
        selected = [e for e in events if f'/{part} ' in e]
        wanted = ['obj', 'dependall'] if part == 'crypto/external/bsd/openssh/lib' else ['obj', 'dependall', 'install']
        check([e.split()[-1] for e in selected] == wanted, f'incomplete native SSH dependency {part}')
    flags, receipt = (out / 'build-flags').read_text(), (out / 'BUILD-INPUTS.txt').read_text()
    check(f'EMBER_LIBEDIT_SOURCE={libedit} ' in flags and f'{libedit}/libedit-source.json' in receipt, 'prepared libedit')
    for envname, path in (('BYACC', 'byacc'), ('ISCSI', 'iscsi'), ('LIBPCAP', 'libpcap'), ('TCP_WRAPPERS', 'tcpwrappers'), ('LIBCBOR', 'libcbor'), ('LIBFIDO2', 'libfido2')):
        check(f'EMBER_{envname}_SOURCE={work}/{path}-source' in flags and
              f'{work}/{path}-source/emberbsd-prepared-source.json' in receipt, f'prepared {path}')
    for feature in ('PAM', 'KERBEROS', 'LDAP', 'YP', 'SKEY', 'HESIOD'):
        check(f'MK{feature}=no' in flags and f'USE_{feature}=no' in flags, f'optional auth feature {feature}')
    order = ['external/bsd/blocklist/lib', 'lib/libwrap', 'crypto/external/bsd/openssh/lib', 'crypto/external/bsd/openssh/bin/sshd']
    positions = [next(i for i, e in enumerate(events) if f'/{p} ' in e) for p in order]
    check(positions == sorted(positions) and len(set(positions)) == 4, 'SSH dependency order')
    inventory = out / 'I18N-INVENTORY.json'
    check(inventory.is_file(), 'separate DATA inventory')
    for part in 'bin/stty usr.sbin/usbdevs sbin/mknod usr.sbin/envstat sbin/nvmectl sbin/wdogctl usr.sbin/acpitools/acpidump'.split():
        selected = [e for e in events if f'/{part} ' in e]
        check(len(selected) == 3 and all(selected[i].endswith(' ' + stage + '\n')
              for i, stage in enumerate(('obj', 'dependall', 'install'))), f'diagnostic component {part}')
    position = lambda part: next(i for i, e in enumerate(events) if part in e)
    check(next(i for i, e in enumerate(events) if '/crypto/external/bsd/netpgp/libmj ' in e and e.endswith(' install\n')) <
          position('/usr.sbin/envstat '), 'libmj installed before envstat')
    check(next(i for i, e in enumerate(events) if '/lib/libc ' in e and e.endswith(' install\n')) <
          position('/sbin/mknod '), 'libc installed before mknod')
    mknod = out / 'dest/sbin/mknod'
    check(mknod.is_file() and f'{digest(mknod)}  {mknod}\n' in (out / 'BINARIES-SHA256.txt').read_text(), 'mknod receipt')
    for part in '''lib/libc external/bsd/wpa/bin/wpa_supplicant external/bsd/dhcpcd/sbin/dhcpcd
      sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl
      usr.bin/stat usr.bin/wall libexec/getty usr.sbin/powerd crypto/external/bsd/netpgp/libmj'''.split():
        check(any(f'/{part} ' in e for e in events), f'missing component {part}')
    check(position('includes') < position('/lib/libc '), 'headers before libc')
    headers = (out / 'header-flags').read_text()
    check('-nostdinc' in headers and f'{out}/dest/usr/include' in headers, 'no old target header fallback')
    check('i18n-inventory-sha256=' + digest(inventory) in receipt, 'DATA inventory pinned')
    for path in [tools / 'bin/nbmkesdb', tools / 'bin/nbmkcsmapper', compiler / 'libgcc.a',
                 compiler / 'libgcc_eh.a', work / 'llvm-source/llvm-source.json']:
        check(digest(path) in receipt, f'hash missing: {path}')
    check(set(role_log.read_text().splitlines()) >= {'git', 'python', 'shasum'}, 'selected native role bypassed')
    if os.environ.get('EMBER_TEST_SMOKE') == 'yes':
        print(f'CIX reduced userland smoke: {checks} assertions, 0 failures')
        raise SystemExit(0)
    check(run(out).returncode != 0, 'must not replace existing output')
    output = work / 'no-libedit'
    result = run(output, EMBER_LIBEDIT_SOURCE='')
    check(result.returncode != 0 and 'EMBER_LIBEDIT_SOURCE' in result.stderr and not output.exists(), 'reject absent libedit')
    (libedit / 'libedit-source.json').unlink()
    output = work / 'partial-libedit'
    result = run(output)
    write(libedit / 'libedit-source.json', '{}\n')
    check(result.returncode != 0 and not output.exists(), 'reject incomplete libedit')
    output = work / 'old-gcc'
    result = run(output, TEST_GCC_VERSION='15.2.0')
    check(result.returncode != 0 and not output.exists(), 'reject non-GCC16 compiler')
    output = work / 'failed'
    result = run(output, TEST_FAIL_COMPONENT='/lib/libc ')
    check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), 'stop on failed libc')
    check('/sbin/ifconfig ' not in (output / 'events').read_text(), 'no programs after failed libc')
    for index, part in enumerate(('external/bsd/blocklist/lib', 'lib/libwrap', 'crypto/external/bsd/openssh/lib',
                                  'crypto/external/bsd/openssh/libexec/sshd-auth')):
        output = work / f'failed-ssh-{index}'
        result = run(output, TEST_FAIL_COMPONENT=f'/{part} ')
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'stop on failed SSH dependency {part}')
    for index, binary in enumerate('sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session usr/libexec/sshd-auth usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb'.split()):
        output = work / f'missing-ssh-{index}'
        result = run(output, TEST_OMIT_BINARY=binary)
        check(result.returncode != 0 and 'Missing required program: ' + binary in result.stderr and
              not (output / 'BUILD-INPUTS.txt').exists(), f'reject missing SSH/account program {binary}')
    for label, binary in (('fsck', 'sbin/fsck_ffs'), ('getty', 'usr/libexec/getty'), ('powerd', 'usr/sbin/powerd')):
        output = work / f'missing-{label}'
        result = run(output, TEST_OMIT_BINARY=binary)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'reject missing {binary}')
    for index, part in enumerate('bin/stty usr.sbin/usbdevs crypto/external/bsd/netpgp/libmj usr.sbin/envstat sbin/nvmectl sbin/wdogctl usr.sbin/acpitools/acpidump'.split()):
        output = work / f'failed-diagnostic-{index}'
        result = run(output, TEST_FAIL_COMPONENT=f'/{part} ')
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'failed diagnostic component {part}')
    for index, binary in enumerate('bin/stty usr/sbin/usbdevs usr/sbin/envstat sbin/nvmectl sbin/wdogctl usr/sbin/acpidump'.split()):
        output = work / f'missing-diagnostic-{index}'
        result = run(output, TEST_OMIT_BINARY=binary)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'missing diagnostic {binary}')
    for stage in ('obj', 'dependall', 'install'):
        output = work / f'failed-mknod-{stage}'
        result = run(output, TEST_FAIL_MKNOD_STAGE=stage)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists() and
              not (output / 'BINARIES-SHA256.txt').exists(), f'failed mknod {stage}')
        failed_events = (output / 'events').read_text().splitlines(keepends=True)
        check('/sbin/mknod ' in failed_events[-1] and failed_events[-1].endswith(f' {stage}\n') and
              not any('/sbin/ifconfig ' in e for e in failed_events), f'stop after failed mknod {stage}')
    output = work / 'missing-mknod'
    result = run(output, TEST_OMIT_BINARY='sbin/mknod')
    check(result.returncode != 0 and 'Missing required program: sbin/mknod' in result.stderr and
          not (output / 'BUILD-INPUTS.txt').exists(), 'reject missing mknod')
    output = work / 'c-runtime'
    result = run(output, TEST_NO_UNWINDER='1')
    check(result.returncode == 0 and 'compiler-unwinder=not-provided' in (output / 'BUILD-INPUTS.txt').read_text(),
          'accept C-only compiler without unwinder')
    for kind in ('invalid', 'dynamic', 'big-endian'):
        output = work / kind
        result = run(output, TEST_ELF_KIND=kind)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'reject {kind} executable')
    for part in ('iconv', 'esdb', 'csmapper'):
        output = work / f'failed-data-{part}'
        result = run(output, TEST_FAIL_COMPONENT=f'/share/i18n/{part} ')
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'reject failed native DATA {part}')
    for index, path in enumerate('iconv/iconv.dir esdb/esdb.dir esdb/esdb.alias esdb/ISO646/ISO646-US.esdb esdb/UTF/UTF-8.esdb csmapper/mapper.dir csmapper/ISO646/ISO646-US%UCS.646'.split()):
        output = work / f'missing-data-{index}'
        result = run(output, TEST_OMIT_DATA=path)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'reject missing DATA {path}')
    for kind in ('metadata', 'path', 'unsupported'):
        output = work / f'unsafe-data-{kind}'
        result = run(output, TEST_BAD_DATA=kind)
        check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), f'reject unsafe DATA {kind}')
    output = work / 'late-data'
    result = run(output, TEST_LATE_DATA_FILE=str(output / 'dest/usr/share/i18n/iconv/iconv.dir'))
    check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), 'reject DATA mutated after inventory')
    profile = work / 'profile'
    profile.mkdir()
    for file in ('cix-p1-build-userland.sh', 'cix-p1-native.py', 'cix-p1-i18n.py'):
        shutil.copyfile(here / file, profile / file)
    output = work / 'late-profile'
    result = selected_runner(profile / 'cix-p1-build-userland.sh', output,
                             {'TEST_LATE_PROFILE_FILE': str(profile / 'cix-p1-i18n.py')})
    check(result.returncode != 0 and not (output / 'BUILD-INPUTS.txt').exists(), 'reject helper mutated during build')
    write(src / 'untracked.h', 'unrecorded source input')
    output = work / 'untracked-source'
    result = run(output)
    check(result.returncode != 0 and not output.exists(), 'reject untracked source')

print(f'CIX reduced userland: {checks} assertions, 0 failures')
