#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the CIX P1 sets-root assembler with synthetic sets and packages."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

here = Path(__file__).parent
prefix = Path(os.environ['EMBER_TEST_NATIVE_PREFIX'])
git = os.environ.get('EMBER_GIT', '/usr/bin/git')
profile = {
    'ember/image/cix-p1-sets-root.sh': here / 'cix-p1-sets-root.sh',
    'ember/image/install-packages.sh': here / 'install-packages.sh',
    'ember/etc/rc.d/ember_packages': here.parent / 'etc/rc.d/ember_packages',
}
os_rev = '1' * 40
ports_rev = '2' * 40
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def entry(name, data=b'', mode=0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    return info, io.BytesIO(data)


def write_tar(path, files, compression):
    with tarfile.open(path, 'w:' + compression) as archive:
        for name, (data, mode) in files.items():
            archive.addfile(*entry(name, data, mode))


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


makedev = b'#!/bin/sh\n[ "$1" = -s ] || exit 1\necho ". type=dir"\n' \
    b'echo "./null type=char device=netbsd,2,2 uname=root gname=wheel mode=0666"\n'
base_files = {
    './bin/sh': (b'sh', 0o555),
    './etc/rc.d/sshd': (b'#!/bin/sh\n', 0o555),
    './etc/mtree/set.base': (b'./bin type=dir uname=root gname=wheel mode=0755\n'
                             b'./bin/sh type=file uname=root gname=wheel mode=0555\n'
                             b'./etc/rc.d/sshd type=file uname=root gname=wheel mode=0555\n'
                             b'./etc/mtree/set.base type=file uname=root gname=wheel mode=0444\n', 0o444),
}
etc_files = {
    './etc/rc.conf': (b'. /etc/defaults/rc.conf\nrc_configured=NO\n', 0o644),
    './etc/master.passwd': (b'root::0:0::0:0:Root:/root:/bin/sh\n', 0o600),
    './etc/group': (b'wheel:*:0:root\n', 0o644),
    './dev/MAKEDEV': (makedev, 0o555),
    './etc/mtree/NetBSD.dist': (b'/set type=dir uname=root gname=wheel mode=0755\n.\n'
                                b'./dev type=dir uname=root gname=wheel mode=0755\n'
                                b'./etc type=dir uname=root gname=wheel mode=0755\n'
                                b'./etc/mtree type=dir uname=root gname=wheel mode=0755\n'
                                b'./etc/rc.d type=dir uname=root gname=wheel mode=0755\n'
                                b'./root type=dir uname=root gname=wheel mode=0755\n'
                                b'./usr type=dir uname=root gname=wheel mode=0755\n'
                                b'./usr/libexec type=dir uname=root gname=wheel mode=0755\n'
                                b'./var type=dir uname=root gname=wheel mode=0755\n', 0o444),
    './etc/mtree/set.etc': (b'./etc/rc.conf type=file uname=root gname=wheel mode=0644\n'
                            b'./etc/master.passwd type=file uname=root gname=wheel mode=0600\n'
                            b'./etc/group type=file uname=root gname=wheel mode=0644\n'
                            b'./dev/MAKEDEV type=file uname=root gname=wheel mode=0555\n'
                            b'./etc/mtree/NetBSD.dist type=file uname=root gname=wheel mode=0444\n'
                            b'./etc/mtree/set.etc type=file uname=root gname=wheel mode=0444\n', 0o444),
}
rcd_script = b'#!/bin/sh\n# PROVIDE: foo\n'


def package(directory, name, arch='aarch64', rcd='foo', label=None, deps=(), cwd='/usr/pkg', mode='0755'):
    contents = '@name %s\n@cwd %s\n' % (label or name, cwd) + ''.join('@pkgdep %s\n' % d for d in deps)
    files = {
        '+CONTENTS': contents.encode(),
        '+BUILD_INFO': ('OPSYS=NetBSD\nMACHINE_ARCH=%s\n' % arch).encode(),
    }
    if rcd:
        install = ('# FILE: /etc/rc.d/%s cr share/examples/rc.d/%s %s\n' % (rcd, rcd, mode)).encode()
        files['+INSTALL'] = install
        files['share/examples/rc.d/' + rcd] = rcd_script
    write_tar(directory / (name + '.tgz'), {k: (v, 0o644) for k, v in files.items()}, 'gz')


fake_makefs = '''#!/bin/sh
printf '%s\\n' "$*" > "$FAKE_LOG.args"
spec=
while [ $# -gt 2 ]; do
	[ "$1" = -F ] && spec=$2
	shift
done
cp "$spec" "$FAKE_LOG.spec"
: > "$1"
'''


class Case:
    def __init__(self, tmp):
        self.tmp = Path(tmp)
        self.src = self.tmp / 'src'
        for rel, origin in profile.items():
            target = self.src / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(origin, target)
        env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_NOSYSTEM='1')
        for args in (['init', '-q'], ['add', '.'],
                     ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                      'commit', '-qm', 'fixture']):
            subprocess.run([git, '-C', str(self.src)] + args, check=True, env=env)
        self.sets = self.tmp / 'sets'
        self.sets.mkdir()
        self.base = dict(base_files)
        self.etc = dict(etc_files)
        self.packages = self.tmp / 'packages'
        self.packages.mkdir()
        package(self.packages, 'foo-1.0', deps=('libbar>=2.0',))
        package(self.packages, 'libbar-2.0', rcd=None)
        self.tools = self.tmp / 'tools'
        (self.tools / 'bin').mkdir(parents=True)
        makefs = self.tools / 'bin/nbmakefs'
        makefs.write_text(fake_makefs)
        makefs.chmod(0o755)
        self.out = self.tmp / 'out'

    def write_sets(self, profile_override=None):
        write_tar(self.sets / 'base.tar.xz', self.base, 'xz')
        write_tar(self.sets / 'etc.tar.xz', self.etc, 'xz')
        sums = {n: sha256(self.sets / n) for n in ('base.tar.xz', 'etc.tar.xz')}
        (self.sets / 'SHA256').write_text(''.join('%s  %s\n' % (v, k) for k, v in sums.items()))
        build = {'status': 'built', 'architecture': 'aarch64', 'source_commit': os_rev,
                 'output_sha256': {'release/sets/' + k: v for k, v in sums.items()}}
        build.update(profile_override or {})
        (self.sets / 'build-profile.json').write_text(json.dumps(build))

    def run(self, label=None, ports=ports_rev):
        env = dict(os.environ, EMBER_HOST_PREFIX=str(prefix), FAKE_LOG=str(self.tmp / 'makefs'),
                   TMPDIR=str(self.tmp), LC_ALL='C')
        env.pop('CIX_ROOT_LABEL', None)
        if label:
            env['CIX_ROOT_LABEL'] = label
        return subprocess.run(['sh', str(self.src / 'ember/image/cix-p1-sets-root.sh'), str(self.sets),
                               str(self.packages), ports, str(self.tools), str(self.out)],
                              env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def rejected(setup, message, reason, label=None, ports=ports_rev):
    with tempfile.TemporaryDirectory() as tmp:
        case = Case(tmp)
        setup(case)
        if not (case.sets / 'SHA256').exists():
            case.write_sets()
        existed = case.out.exists()
        result = case.run(label, ports)
        check(result.returncode != 0, message + ': assembler accepted it')
        check(existed or not (case.out / 'root.fs').exists(), message + ': image was created')
        check(reason in result.stderr, '%s: rejected for another reason: %s' % (message, result.stderr))


# Success: the image spec gives target metadata to every added file.
with tempfile.TemporaryDirectory() as tmp:
    case = Case(tmp)
    case.write_sets()
    result = case.run()
    check(result.returncode == 0, 'Valid inputs failed: ' + result.stderr)
    root = case.out / 'root'
    spec = (case.out / 'root.spec').read_text()
    check(spec == (case.tmp / 'makefs.spec').read_text(), 'makefs received another spec')
    args = (case.tmp / 'makefs.args').read_text()
    for word in ('-Zx', '-s 4g', 'density=8192', '-t ffs'):
        check(word in args, 'makefs arguments lack ' + word)
    check('./etc/rc.d/foo type=file uname=root gname=wheel mode=0755' in spec, 'rc.d script lacks its mode')
    check((root / 'etc/rc.d/foo').read_bytes() == rcd_script, 'rc.d script differs from the package')
    check((root / 'var/ember/packages/rc.d.list').read_text() == 'foo\n', 'rc.d list is wrong')
    rc_conf = (root / 'etc/rc.conf').read_text()
    for line in ('ember_packages=YES', 'foo=NO', 'wpa_supplicant=NO', 'rc_configured=YES'):
        check(line in rc_conf.splitlines(), 'rc.conf lacks ' + line)
    check(rc_conf.count('./etc/rc.conf type') == 0 and spec.count('./etc/rc.conf type=') == 1,
          'rc.conf has duplicate metadata')
    check((root / 'etc/fstab').read_text().startswith('NAME=ember-cix-sets / ffs rw,log,noatime 1 1\n'),
          'fstab does not select the wedge')
    check('./dev/null type=char' in spec, 'device nodes are missing')
    manifest = (root / 'var/ember/packages/packages.sha256').read_text().split('\n')
    check(sorted(line.split()[1] for line in manifest if line) == ['foo-1.0.tgz', 'libbar-2.0.tgz'],
          'package manifest is wrong')
    for line in manifest:
        if line:
            digest, name = line.split()
            check(sha256(root / 'var/ember/packages' / name) == digest, 'bundle copy differs: ' + name)
    inputs = (case.out / 'ROOT-INPUTS.txt').read_text()
    check('os=' + os_rev in inputs and 'ports=' + ports_rev in inputs, 'inputs lack revisions')
    check(str(case.tmp) not in (root / 'etc/ember-build').read_text(), 'build host path in the image')
    verify = subprocess.run(['sh', str(here / 'install-packages.sh'), '--verify', str(root / 'var/ember/packages')],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(verify.returncode == 0, 'bundle verification failed: ' + verify.stderr)
    (root / 'var/ember/packages/extra.tgz').write_bytes(b'x')
    verify = subprocess.run(['sh', str(here / 'install-packages.sh'), '--verify', str(root / 'var/ember/packages')],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(verify.returncode != 0, 'unlisted archive passed verification')

# Rejections happen before the image exists.
def bad_set_hash(case):
    case.write_sets()
    (case.sets / 'SHA256').write_text('0' * 64 + '  base.tar.xz\n')


def bad_profile_hash(case):
    case.write_sets({'output_sha256': {'base.tar.xz': '0' * 64, 'etc.tar.xz': '0' * 64}})


def wrong_arch_profile(case):
    case.write_sets({'architecture': 'x86_64'})


def collision(case):
    case.base['./bin/SH'] = (b'other', 0o555)


def unlisted(case):
    case.base['./bin/extra'] = (b'extra', 0o555)


def wrong_arch_package(case):
    package(case.packages, 'baz-1.0', arch='x86_64', rcd=None)


def renamed_package(case):
    package(case.packages, 'baz-1.0', rcd=None, label='other-1.0')


def base_rcd(case):
    package(case.packages, 'baz-1.0', rcd='sshd')


def empty_bundle(case):
    for archive in case.packages.iterdir():
        archive.unlink()


def dirty_profile(case):
    with open(case.src / 'ember/image/install-packages.sh', 'a') as script:
        script.write('# local change\n')


def bad_rcd_mode(case):
    package(case.packages, 'baz-1.0', rcd='baz', mode='755')


def other_prefix(case):
    package(case.packages, 'baz-1.0', rcd=None, cwd='/opt/pkg')


def symlinked_package(case):
    (case.packages / 'baz-1.0.tgz').symlink_to(case.packages / 'libbar-2.0.tgz')


def missing_dependency(case):
    package(case.packages, 'baz-1.0', rcd=None, deps=('qux>=1',))


def existing_output(case):
    case.out.mkdir()


for setup, message, reason in (
        (collision, 'Case collision', 'collide on a case-insensitive volume'),
        (unlisted, 'Unlisted file', 'without target metadata'),
        (bad_set_hash, 'Set checksum mismatch', 'Checksum mismatch: base.tar.xz'),
        (bad_profile_hash, 'Profile checksum mismatch', 'Checksum mismatch: base.tar.xz'),
        (wrong_arch_profile, 'Wrong release architecture', 'not a built aarch64 release'),
        (wrong_arch_package, 'Wrong package architecture', 'not for NetBSD/aarch64: baz-1.0.tgz'),
        (renamed_package, 'Renamed package', 'differs from its archive: baz-1.0.tgz'),
        (base_rcd, 'rc.d script replacing a base script', 'Duplicate rc.d script: sshd'),
        (bad_rcd_mode, 'Bad rc.d mode', 'Bad rc.d mode: 755'),
        (other_prefix, 'Other package prefix', 'prefix is not /usr/pkg: baz-1.0.tgz'),
        (symlinked_package, 'Symlinked bundle entry', 'not a regular file'),
        (missing_dependency, 'Missing dependency', 'No bundle package matches qux>=1 (baz-1.0)'),
        (empty_bundle, 'Empty bundle', 'The package bundle is empty.'),
        (existing_output, 'Existing output', 'NEW_OUTPUT exists.')):
    rejected(setup, message, reason)
rejected(lambda case: None, 'Unsafe label', 'Unsafe root label.', label='Bad_Label')
rejected(lambda case: None, 'Short Ports revision', 'PORTS_REV must be a full commit ID.', ports='abc123')
# A dirty profile stops in git diff --quiet, which prints nothing.
rejected(dirty_profile, 'Dirty profile checkout', '')
print('CIX P1 sets-root contract checks passed: %d' % checks)
