#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real CIX root stager with synthetic inputs and image tools."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

here = Path(__file__).parent
script = Path(os.environ.get('ROOT_SCRIPT', here / 'cix-p1-root.sh')).resolve()
assert script.is_file(), 'Root stager is missing'
profile_files = '''cix-p1-root.sh cix-p1-root.subr cix-p1-network.subr cix-p1-ssh.subr
  cix-p1-power.subr cix-p1-power-button cix-p1-rc cix-p1-rc.shutdown cix-p1-console cix-p1-i18n.py'''.split()
profile_data = {p: (script if p == 'cix-p1-root.sh' else script.parent / p).read_bytes() for p in profile_files}
required = '''sbin/init sbin/ifconfig sbin/fsck sbin/fsck_ffs sbin/mount sbin/mount_tmpfs sbin/shutdown
  sbin/rndctl usr/sbin/wpa_supplicant usr/sbin/wpa_cli sbin/dhcpcd bin/sh bin/stty usr/sbin/usbdevs
  usr/bin/stat usr/libexec/getty usr/sbin/powerd sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session
  usr/libexec/sshd-auth usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb usr/sbin/envstat sbin/nvmectl
  sbin/wdogctl usr/sbin/acpidump'''.split()
runtime = required + ['usr/bin/wall', 'usr/bin/netstat']
i18n_required = '''iconv/iconv.dir esdb/esdb.dir esdb/esdb.alias esdb/ISO646/ISO646-US.esdb
  esdb/UTF/UTF-8.esdb csmapper/mapper.dir csmapper/ISO646/ISO646-US%UCS.646'''.split()
i18n_data = sorted('usr/share/i18n/' + p for p in i18n_required + '''esdb/esdb.dir.db esdb/esdb.alias.db
  csmapper/mapper.dir.db csmapper/charset.pivot csmapper/charset.pivot.pvdb'''.split())
diagnostic = ['usb'] + [f'usb{i}' for i in range(20)] + ['plcom1', 'plcom3', 'nvme0']
fw_hash = '36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26'
prefix = Path(os.environ['EMBER_TEST_NATIVE_PREFIX'])
shell = str(prefix / 'bin/mksh')
python = str(prefix / 'bin/python3.15')
host_path = os.environ['PATH']
real_shasum = str(prefix / 'bin/shasum')
git = os.environ.get('EMBER_GIT', '/usr/bin/git')
native_source = script.parent.parent.parent
base_env = dict(os.environ, PATH=host_path, LC_ALL='C', GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_NOSYSTEM='1')


def command(argv, env=None, input=None):
    result = subprocess.run(argv, env=env or base_env, input=input, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f'Fixture command failed: {argv[0]}: {result.stderr}')
    return result.stdout


native_makedev = command(['awk', '-f', str(native_source / 'etc/MAKEDEV.awk'), str(native_source / 'etc/MAKEDEV.tmpl')],
                         dict(base_env, MACHINE='aarch64', MACHINE_ARCH='aarch64', NETBSDSRCDIR=str(native_source)))
diagnostic_output = command([shell, '-s', '--', '-s', *diagnostic],
                            dict(base_env, MACHINE='aarch64', MACHINE_ARCH='aarch64'), native_makedev)
diagnostic_spec = [line for line in diagnostic_output.splitlines(keepends=True) if line.startswith('./')]
assert sorted(line.split()[0] for line in diagnostic_spec) == sorted('./' + n for n in diagnostic + ['dplcom1', 'dplcom3'])
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def write(path, data, mode=0o644):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())
    path.chmod(mode)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git_commit(path):
    for args in (['init', '-q'], ['add', '.'], ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'fixture']):
        command([git, '-C', str(path), *args])
    return command([git, '-C', str(path), 'rev-parse', 'HEAD']).strip()


def manifest(f, paths=runtime):
    rows = []
    for rel in paths:
        path = f['cohort'] / 'dest' / rel
        rows.append(f'{digest(path)}  {path}\n')
    write(f['cohort'] / 'BINARIES-SHA256.txt', ''.join(rows))


def pin_data(f, data):
    path = f['cohort'] / 'I18N-INVENTORY.json'
    write(path, json.dumps(data, indent=2) + '\n')
    receipt = f['cohort'] / 'BUILD-INPUTS.txt'
    lines = [s for s in receipt.read_text().splitlines(keepends=True) if not s.startswith('i18n-inventory-sha256=')]
    write(receipt, ''.join(lines) + 'i18n-inventory-sha256=' + digest(path) + '\n')


def data_manifest(f):
    parents = set()
    for path in i18n_data:
        parts = path.split('/')
        parents.update('/'.join(parts[:n]) for n in range(1, len(parts)))
    metalog = f['metalog'].read_text().splitlines()
    entries = []
    for path in sorted(parents | set(i18n_data)):
        row = {'path': path, 'metadata': [s for s in metalog if s.startswith(f'./{path} ')][-1].strip()}
        if (f['cohort'] / 'dest' / path).is_file():
            row['sha256'] = digest(f['cohort'] / 'dest' / path)
        entries.append(row)
    pin_data(f, {'format': 1, 'source': f['revision'], 'entries': entries})


def fixture(base, name):
    directory = base / name
    directory.mkdir()
    f = {key: directory / value for key, value in [('dir', '.'), ('cohort', 'cohort'), ('src', 'source'),
         ('tools', 'tools'), ('firmware', 'firmware.bin'), ('out', 'output'), ('log', 'calls.log')]}
    hooks = f['src'] / 'external/bsd/dhcpcd/dist/hooks'
    write(hooks / 'dhcpcd-run-hooks', f'#!{shell}\nexit 0\n', 0o755)
    write(hooks / '20-resolv.conf', '# synthetic DNS hook\n')
    write(f['src'] / 'sys/dev/ic/rtw88-firmware.LICENSE', 'Synthetic firmware licence\n')
    write(f['src'] / 'external/realtek/rtw89/dist/LICENCE.rtlwifi_firmware.txt', 'Synthetic firmware licence\n')
    write(f['src'] / 'etc/group', 'wheel:*:0:root\noperator:*:5:root\ntty:*:4:\nkmem:*:2:\n')
    f['revision'] = git_commit(f['src'])
    f['profile'] = directory / 'profile'
    for file, data in profile_data.items():
        write(f['profile'] / 'ember/image' / file, data)
    f['profile_revision'] = git_commit(f['profile'])
    f['script'] = f['profile'] / 'ember/image/cix-p1-root.sh'
    for p in ('bin', 'sbin', 'usr/bin', 'usr/sbin', 'usr/libexec'):
        (f['cohort'] / 'dest' / p).mkdir(parents=True, exist_ok=True)
    for p in runtime:
        write(f['cohort'] / 'dest' / p, f'#!{shell}\nexit 0\n', 0o755)
    f['metalog'] = f['cohort'] / 'dest/METALOG'
    special = {'sbin/shutdown': ('operator', '04554'), 'usr/bin/wall': ('tty', '02555'), 'usr/bin/netstat': ('kmem', '02555')}
    metadata = []
    for p in runtime:
        group, mode = special.get(p, ('wheel', '0755'))
        file = f['cohort'] / 'dest' / p
        metadata.append(f'./{p} type=file uname=root gname={group} mode={mode} size={file.stat().st_size} time=1.0 sha256={digest(file)}\n')
    write(f['metalog'], ''.join(metadata))
    write(f['cohort'] / 'BUILD-INPUTS.txt', f'source={f["revision"]}\ngcc=16.2.0\ntarget=aarch64--netbsd\nprofile=reduced-static-c-development\n')
    manifest(f)
    for p in i18n_data:
        write(f['cohort'] / 'dest' / p, 'synthetic native DATA ' + p)
    parents = set()
    for path in i18n_data:
        parts = path.split('/')
        parents.update('/'.join(parts[:n]) for n in range(1, len(parts)))
    with f['metalog'].open('a') as output:
        for p in parents:
            output.write(f'./{p} type=dir uname=root gname=wheel mode=0755\n')
        for p in i18n_data:
            file = f['cohort'] / 'dest' / p
            output.write(f'./{p} type=file uname=root gname=wheel mode=0444 size={file.stat().st_size} time=1.0 sha256={digest(file)}\n')
    data_manifest(f)
    spec = f'''#!{shell}
case "$*" in
  '-s ramdisk wscons'|'-s ramdisk wscons sysmon'|'-s ramdisk wscons sysmon ptm {' '.join(diagnostic)}') ;;
  *) echo 'Only fixture spec generation is permitted.' >&2; exit 92;;
esac
'''
    write(f['cohort'] / 'obj/etc/MAKEDEV', spec + native_makedev, 0o755)
    write(f['firmware'], 'synthetic-approved-firmware\n')
    write(f['cohort'] / 'dest/etc/wpa_supplicant.conf', 'SYNTHETIC_PROFILE_MUST_NOT_SHIP\n', 0o600)
    fake = directory / 'fakebin'
    fake.mkdir()
    (fake / 'git').symlink_to(git)
    tool = (here / 'fixtures/cix-root-tools.py').read_text().replace('#!/usr/bin/env python3', '#!' + python, 1)
    for name in 'shasum dd diskutil mount newfs newfs_ffs gpt dkctl'.split():
        write(fake / name, tool, 0o755)
    for name in ('nbmakefs', 'nbmtree', 'nbpwd_mkdb'):
        write(f['tools'] / 'bin' / name, tool, 0o755)
    f['env'] = dict(base_env, PATH=f'{fake}:{host_path}', EMBER_HOST_PREFIX=str(prefix), CALL_LOG=str(f['log']),
                    REAL_SHASUM=real_shasum, FIXTURE_FIRMWARE=str(f['firmware']), FIXTURE_FW_HASH=fw_hash,
                    FIXTURE_OUT=str(f['out']), FIXTURE_SOURCE=str(f['src']))
    return f


def run_stager(f):
    result = subprocess.run([shell, str(f['script']), *(str(f[k]) for k in ('cohort', 'src', 'tools', 'firmware', 'out'))],
                            env=f['env'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    calls = [json.loads(line) for line in f['log'].read_text().splitlines()] if f['log'].exists() else []
    return result.returncode == 0, result.stdout + result.stderr, calls


def reject(f, label):
    success, output, calls = run_stager(f)
    check(not success, f'{label}: rejected: {output}')
    check(not f['out'].exists(), label + ': output not created')
    check(not any(a[0] in ('nbmakefs', 'nbmtree', 'nbpwd_mkdb') for a in calls), label + ': stopped before image tools')


with tempfile.TemporaryDirectory(prefix='ember-cix-root-image-') as name:
    base = Path(name)
    f = fixture(base, 'forbidden-device')
    result = subprocess.run([python, '-O', str(f['tools'] / 'bin/nbmakefs'), '/dev/not-a-real-device'],
                            env=f['env'], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(result.returncode != 0 and 'device argument passed to formatter' in result.stderr,
          'fixture rejects a device path even under optimized Python')
    check(not f['out'].exists(), 'forbidden formatter input creates no output')
    f = fixture(base, 'existing-output')
    sentinel = f['out'] / 'sentinel'
    write(sentinel, 'leave-existing-output-alone')
    before = digest(sentinel)
    success, _, calls = run_stager(f)
    check(not success, 'existing output rejected')
    check([p.name for p in f['out'].iterdir()] == ['sentinel'] and digest(sentinel) == before, 'existing output unchanged')
    check(not calls, 'existing output rejected before input tools')
    f = fixture(base, 'existing-output-link')
    f['out'].symlink_to(f['dir'] / 'missing-target')
    success, _, calls = run_stager(f)
    check(not success and f['out'].is_symlink(), 'dangling output symlink rejected unchanged')
    check(not calls, 'output symlink rejected before input tools')
    for name, path in (('unverified-cohort', 'BUILD-INPUTS.txt'), ('missing-binary-receipt', 'BINARIES-SHA256.txt'),
                       ('missing-data-receipt', 'I18N-INVENTORY.json')):
        f = fixture(base, name)
        (f['cohort'] / path).unlink()
        reject(f, name)
    for index, path in enumerate(i18n_required):
        f = fixture(base, f'missing-data-{index}')
        (f['cohort'] / 'dest/usr/share/i18n' / path).unlink()
        reject(f, 'missing required DATA ' + path)
    for name in ('changed-data', 'unrecorded-data', 'data-symlink', 'data-dir-symlink', 'changed-data-metadata'):
        f = fixture(base, name)
        target = f['cohort'] / 'dest' / i18n_data[0]
        if name == 'changed-data':
            write(target, 'tampered')
        elif name == 'unrecorded-data':
            write(f['cohort'] / 'dest/usr/share/i18n/esdb/extra', 'unrecorded')
        elif name == 'data-symlink':
            target.unlink()
            target.symlink_to('/etc/passwd')
        elif name == 'data-dir-symlink':
            (f['cohort'] / 'dest/usr/share/i18n/alias').symlink_to('/etc')
        else:
            with f['metalog'].open('a') as output:
                output.write(f'./{i18n_data[0]} type=file uname=root gname=wheel mode=0777\n')
        reject(f, name)
    for name in ('unrecorded-allowed-data', 'duplicate-data-entry', 'malformed-data-path',
                 'malicious-data-metadata', 'unsupported-data-schema'):
        f = fixture(base, name)
        data = json.loads((f['cohort'] / 'I18N-INVENTORY.json').read_text())
        if name == 'unrecorded-allowed-data':
            data['entries'] = [row for row in data['entries'] if not row['path'].endswith('/esdb.dir.db')]
        elif name == 'duplicate-data-entry':
            data['entries'].append(data['entries'][-1])
        elif name == 'malformed-data-path':
            data['entries'][0]['path'] = '../escape'
        elif name == 'malicious-data-metadata':
            data['entries'][-1]['metadata'] += ' content=/etc/passwd'
        else:
            data['extra'] = 'unreviewed'
        pin_data(f, data)
        reject(f, name)
    f = fixture(base, 'duplicate-json-key')
    path = f['cohort'] / 'I18N-INVENTORY.json'
    write(path, path.read_text().replace('"format": 1', '"format": 1, "format": 1', 1))
    receipt = f['cohort'] / 'BUILD-INPUTS.txt'
    lines = [s for s in receipt.read_text().splitlines(keepends=True) if not s.startswith('i18n-inventory-sha256=')]
    write(receipt, ''.join(lines) + 'i18n-inventory-sha256=' + digest(path) + '\n')
    reject(f, 'duplicate JSON key with correctly re-pinned inventory')
    for name, line in (('malicious-native-data-path', './usr/share/i18n/../escape type=file uname=root gname=wheel mode=0444'),
                       ('malicious-native-data-field', f'./{i18n_data[0]} type=file uname=root gname=wheel mode=0444 content=/etc/passwd')):
        f = fixture(base, name)
        with f['metalog'].open('a') as output:
            output.write(line + '\n')
        reject(f, name)
    f = fixture(base, 'missing-installed-optional-data')
    (f['cohort'] / 'dest/usr/share/i18n/esdb/esdb.dir.db').unlink()
    data = json.loads((f['cohort'] / 'I18N-INVENTORY.json').read_text())
    data['entries'] = [row for row in data['entries'] if row['path'] != 'usr/share/i18n/esdb/esdb.dir.db']
    pin_data(f, data)
    reject(f, 'native METALOG installed optional DATA is missing')
    f = fixture(base, 'mismatched-source')
    write(f['cohort'] / 'BUILD-INPUTS.txt', 'source=' + 'f' * 40 + '\n')
    reject(f, 'source revision mismatch')
    f = fixture(base, 'dirty-source')
    write(f['src'] / 'etc/group', 'modified after source receipt')
    reject(f, 'dirty tracked source')
    f = fixture(base, 'dirty-profile')
    with f['script'].open('a') as output:
        output.write('# modified after profile commit\n')
    reject(f, 'dirty tracked image profile')
    for index, rel in enumerate('sbin/fsck sbin/mount_tmpfs usr/sbin/wpa_supplicant usr/libexec/getty usr/sbin/powerd bin/stty usr/sbin/usbdevs sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session usr/libexec/sshd-auth usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb'.split()):
        f = fixture(base, f'missing-runtime-{index}')
        (f['cohort'] / 'dest' / rel).unlink()
        manifest(f, [p for p in runtime if p != rel])
        reject(f, 'missing runtime ' + rel)
    for name in ('binary-mismatch', 'firmware-mismatch', 'missing-image-tool', 'missing-device-spec',
                 'partial-manifest', 'unlisted-runtime', 'duplicate-manifest', 'outside-manifest',
                 'runtime-symlink', 'missing-metadata', 'missing-runtime-metadata'):
        f = fixture(base, name)
        if name == 'binary-mismatch':
            with (f['cohort'] / 'dest/sbin/init').open('a') as output:
                output.write('changed after binary receipt\n')
        elif name == 'firmware-mismatch':
            write(f['firmware'], 'synthetic-corrupt-firmware')
        elif name == 'missing-image-tool':
            (f['tools'] / 'bin/nbmakefs').unlink()
        elif name == 'missing-device-spec':
            (f['cohort'] / 'obj/etc/MAKEDEV').unlink()
        elif name == 'partial-manifest':
            manifest(f, ['bin/sh'])
        elif name == 'unlisted-runtime':
            write(f['cohort'] / 'dest/usr/bin/unverified', f'#!{shell}\nexit 0\n', 0o755)
        elif name == 'duplicate-manifest':
            path = f['cohort'] / 'BINARIES-SHA256.txt'
            with path.open('a') as output:
                output.write(path.read_text().splitlines(keepends=True)[0])
        elif name == 'outside-manifest':
            outside = f['dir'] / 'outside'
            write(outside, 'not a selected runtime input')
            with (f['cohort'] / 'BINARIES-SHA256.txt').open('a') as output:
                output.write(f'{digest(outside)}  {outside}\n')
        elif name == 'runtime-symlink':
            (f['cohort'] / 'dest/usr/bin/unverified-alias').symlink_to('stat')
        elif name == 'missing-metadata':
            f['metalog'].unlink()
        else:
            write(f['metalog'], ''.join(s for s in f['metalog'].read_text().splitlines(keepends=True)
                                        if not s.startswith('./sbin/shutdown ')))
        reject(f, name)
    for which in ('source', 'profile', 'console-wrapper', 'data-helper', 'data-source', 'ssh-helper'):
        f = fixture(base, f'late-{which}-change')
        asset = {'source': f['src'] / 'etc/group', 'profile': f['profile'] / 'ember/image/cix-p1-network.subr',
                 'console-wrapper': f['profile'] / 'ember/image/cix-p1-console',
                 'data-helper': f['profile'] / 'ember/image/cix-p1-i18n.py',
                 'data-source': f['cohort'] / 'dest' / i18n_data[0],
                 'ssh-helper': f['profile'] / 'ember/image/cix-p1-ssh.subr'}[which]
        f['env']['MUTATE_TRACKED_ASSET'] = str(asset)
        success, _, _ = run_stager(f)
        check(not success, f'late {which} mutation rejects final acceptance')
        check(not (f['out'] / 'ROOT-INPUTS.txt').exists() and not (f['out'] / 'ROOT-SHA256.txt').exists(),
              f'late {which} mutation emits no accepted image receipt')
    for name, key, value in (('late-staged-data', 'MUTATE_STAGED_DATA', i18n_data[0]),
                             ('extra-staged-data', 'EXTRA_STAGED_DATA', '1')):
        f = fixture(base, name)
        f['env'][key] = value
        success, _, _ = run_stager(f)
        check(not success and not (f['out'] / 'ROOT-INPUTS.txt').exists() and
              not (f['out'] / 'ROOT-SHA256.txt').exists(), name + ' prevents accepted receipts')
    f = fixture(base, 'readonly-data-directory')
    with f['metalog'].open('a') as output:
        output.write('./usr/share/i18n/esdb type=dir uname=root gname=wheel mode=0555\n')
    data_manifest(f)
    success, output, _ = run_stager(f)
    check(success, 'read-only DATA directory permits copying children: ' + output)
    (f['out'] / 'root/usr/share/i18n/esdb').chmod(0o755)
    f = fixture(base, 'success')
    success, output, calls = run_stager(f)
    check(success, 'verified synthetic cohort creates staged image: ' + output)
    root = f['out'] / 'root'
    check((root / 'etc/fstab').read_text() == 'NAME=ember-cix-root / ffs rw 1 1\n', 'expected stable root NAME')
    console = next((line for line in (root / 'etc/ttys').read_text().splitlines() if line.startswith('console ')), None)
    check(console is not None and shlex.split(console) == ['console', '/usr/libexec/getty ember-cix', 'vt100', 'on', 'secure'],
          'init starts getty with selected profile as one command field')
    getty = root / 'etc/gettytab'
    entries = re.sub(r'\\\n[\t ]*', '', getty.read_text()).splitlines() if getty.is_file() else []
    entry = next((line.strip() for line in entries if not line.strip().startswith('#') and
                  'ember-cix' in line.strip().split(':')[0].split('|')), '')
    check(bool(entry) and all(cap in entry.split(':') for cap in ('al=root', 'lo=/usr/libexec/ember-console',
                                                                  'sp#115200', 'np', 'ce', 'ck')),
          'getty creates console session and selects root wrapper')
    console = root / 'usr/libexec/ember-console'
    check(console.read_bytes() == profile_data['cix-p1-console'] and console.stat().st_mode & 0o7777 == 0o755,
          'exact pinned console supervisor staged with mode 0755')
    check((root / 'etc/cix-ssh.subr').read_bytes() == profile_data['cix-p1-ssh.subr'], 'pinned SSH helper staged')
    for name in ('etc/ssh', 'var/chroot/sshd'):
        path = root / name
        check(path.is_dir() and path.stat().st_mode & 0o7777 == 0o755 and not list(path.iterdir()), 'empty safe SSH directory: ' + name)
    check((root / 'etc/master.passwd').read_text() == 'root:*:0:0::0:0:Development console:/root:/bin/sh\n', 'no provisioned accounts or passwords')
    check(not (root / 'etc/ssh/sshd_config').exists() and not list((root / 'etc/ssh').iterdir()), 'no SSH config or host key')
    spec = (f['out'] / 'root.spec').read_text()
    check(any(line.startswith('./dev/ptm ') for line in spec.splitlines()), 'native PTY multiplexor included')
    check(not (root / 'etc/wpa_supplicant.conf').exists(), 'Wi-Fi template not shipped')
    power = root / 'etc/powerd/scripts/power_button'
    check(power.is_file() and os.access(power, os.X_OK), 'executable power-button policy')
    check(not any(b'SYNTHETIC_PROFILE_MUST_NOT_SHIP' in p.read_bytes() for p in root.rglob('*')
                  if not p.is_symlink() and p.is_file()), 'synthetic profile absent from staged files')
    check((root / 'root').stat().st_mode & 0o7777 == 0o700 and (root / 'tmp').stat().st_mode & 0o7777 == 0o1777,
          'root home and temporary directory modes')
    check('/set type=file mode=0755 uid=0 gid=0\n' in spec, 'target ownership defaults are root')
    lines = spec.splitlines(keepends=True)
    check(any(s.split()[0] == './dev/console' and 'type=char' in s and 'device=netbsd,2,0' in s and 'mode=600' in s
              for s in lines if s.split()), 'MAKEDEV console node under dev')
    check(any(s.split()[0] == './dev/power' and 'type=char' in s and 'device=netbsd,101,2' in s and 'mode=640' in s
              for s in lines if s.split()), 'native AArch64 sysmon power device')
    for name in ('sysmon', 'watchdog'):
        check(any(s.split()[0] == './dev/' + name and 'type=char' in s and 'device=netbsd,101,' in s
                  for s in lines if s.split()), 'native AArch64 sysmon device: ' + name)
    for native in diagnostic_spec:
        expected = native.replace('./', './dev/', 1)
        check(expected in lines, 'native diagnostic node specification retained: ' + expected.split()[0])
    paths = [line.split()[0] for line in lines if line.split()]
    selected = ['./dev/' + name for name in ['usb'] + [f'usb{i}' for i in range(20)]]
    check(not (set(p for p in paths if re.fullmatch(r'\./dev/usb\d*', p)) - set(selected)), 'no extra USB controller')
    check('./dev/plcom2' not in paths and './dev/dplcom2' not in paths, 'DEBUG UART nodes not staged')
    for binary in 'bin/stty usr/sbin/usbdevs usr/sbin/envstat sbin/nvmectl sbin/wdogctl usr/sbin/acpidump'.split():
        check(os.access(root / binary, os.X_OK), 'native diagnostic tool staged: ' + binary)
    check('\n./console ' not in spec, 'device spec does not create a root console path')
    for path, (group, mode) in {'./sbin/shutdown': ('operator', 0o4554), './usr/bin/wall': ('tty', 0o2555),
                                './usr/bin/netstat': ('kmem', 0o2555)}.items():
        entries = [s for s in lines if s.split() and s.split()[0] == path]
        fields = dict(word.split('=', 1) for word in entries[-1].split()[1:]) if entries else {}
        check(fields.get('uname') == 'root' and fields.get('gname') == group and int(fields.get('mode', '0'), 8) == mode,
              'native ownership/mode preserved for ' + path)
    group = (root / 'etc/group').read_text()
    check('tty:*:4:\n' in group and 'kmem:*:2:\n' in group, 'native groups retained')
    for p in i18n_data:
        data = root / p
        check(data.is_file() and data.read_bytes() == (f['cohort'] / 'dest' / p).read_bytes(), 'exact verified DATA: ' + p)
        entry = [s for s in lines if s.split() and s.split()[0] == './' + p][-1]
        check('uname=root gname=wheel mode=0444' in entry and data.stat().st_mode & 0o7777 == 0o444,
              'native DATA metadata retained: ' + p)
    check((root / 'libdata/firmware/rtwce/LICENCE.rtlwifi_firmware.txt').is_file(), 'firmware licence staged')
    check((f['out'] / 'ROOT-INPUTS.txt').read_text() == f'userland={f["revision"]}\nprofile={f["profile_revision"]}\n',
          'image receipt identifies userland and profile commits')
    makefs = [a for a in calls if a[0] == 'nbmakefs']
    check(len(makefs) == 1 and makefs[0][makefs[0].index('-F') + 1] == str(f['out'] / 'root.spec'),
          'image format uses explicit target ownership/device spec')
    check(not any(a[0] in ('dd', 'diskutil', 'mount', 'newfs', 'newfs_ffs', 'gpt', 'dkctl') for a in calls),
          'no media action occurred')

print(f'CIX root image: {checks} checks, 0 failures')
