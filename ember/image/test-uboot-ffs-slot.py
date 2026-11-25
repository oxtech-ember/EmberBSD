#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the U-Boot slot adapter without any device nodes or live mounts."""

import hashlib
import importlib.machinery
import importlib.util
import io
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import Mock, call, patch


IMAGE = Path(__file__).resolve().parent
loader = importlib.machinery.SourceFileLoader('uboot_slot', str(IMAGE / 'uboot-ffs-slot'))
slot = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(slot)


class SlotTests(unittest.TestCase):
    def setUp(self):
        self.work = Path(self.enterContext(tempfile.TemporaryDirectory())).resolve()
        self.boot = self.work / 'boot'
        self.boot.mkdir()
        self.state = self.work / 'state'
        self.state.mkdir()
        self.enterContext(patch.object(slot, 'BOOT', self.boot))
        self.enterContext(patch.object(slot, 'STATE', self.state))
        self.enterContext(patch.object(slot, 'read_profile', return_value={
            'model': 'Test board', 'watchdog': 'testwdt0'}))
        self.commands = self.enterContext(patch.object(slot, 'run'))
        self.prepare = self.enterContext(patch.object(slot, 'prepare'))
        self.enterContext(patch.object(slot, 'output', side_effect=self.output))
        self.root = 'dk1'
        self.labels = {'dk0': ('EFI', 'msdos'), 'dk1': ('netbsd-root', 'ffs'),
                       'dk2': ('ember-root-b', 'ffs')}
        self.boot_mount = '/dev/dk0 on /boot type msdos (local)'
        self.chosen = ''
        self.watchdog = 'testwdt0, 16 second period [armed, external tickle]'
        self.receipt = b'{"generation":2}\n'
        (self.state / 'release.json').write_bytes(self.receipt)
        for key, value in ((slot.GOOD, b'A'), (slot.BACK, b'A'), (slot.NEXT, b'-')):
            (self.boot / key).write_bytes(value)

    def output(self, *args):
        if args == ('/sbin/sysctl', '-n', 'hw.model'):
            return 'Test board'
        if args == ('/sbin/sysctl', '-n', 'kern.root_device'):
            return self.root
        if args == ('/sbin/dkctl', self.root, 'getwedgeinfo'):
            return f'{self.root} at ld0: {self.labels[self.root][0]}\n'
        if args == ('/sbin/dkctl', 'ld0', 'listwedges'):
            return '\n'.join(f'{dev}: {label}, 1000 blocks at {index * 1000}, type: {fs}'
                             for index, (dev, (label, fs)) in enumerate(self.labels.items()))
        if args == ('/sbin/mount',):
            return f'/dev/{self.root} on / type ffs (local)\n{self.boot_mount}'
        if args == ('/usr/sbin/ofctl', '-p', '/chosen'):
            return self.chosen
        if args == ('/sbin/wdogctl',):
            return self.watchdog
        self.fail(f'unexpected query: {args}')

    def invoke(self, *args):
        with patch.object(sys, 'argv', ['ember-os-slot', *args]):
            slot.main()

    def test_exact_wedges_and_mounted_boot(self):
        status, devices = slot.layout({'model': 'Test board'})
        self.assertEqual(devices, {'A': 'dk1', 'B': 'dk2'})
        self.assertEqual(status['running'], 'A')
        original = dict(self.labels)
        for name, values in [('dk3', ('netbsd-root', 'ffs')), ('dk2', ('wrong-root', 'ffs')),
                             ('dk2', ('ember-root-b', 'swap'))]:
            with self.subTest(values=values):
                self.labels = dict(original, **{name: values})
                with self.assertRaises((ValueError, KeyError)):
                    self.invoke('prepare', 'B', '/fixture')
        self.labels = original
        for mount in ('/dev/dk7 on /boot type msdos (local)', '/dev/dk0 on /boot type ffs (local)'):
            with self.subTest(mount=mount):
                self.boot_mount = mount
                with self.assertRaises(ValueError):
                    self.invoke('prepare', 'B', '/fixture')
        self.assertFalse(self.prepare.called)
        self.commands.assert_not_called()

    def test_active_root_or_unconfirmed_root_never_reaches_preparation(self):
        with self.assertRaises(ValueError):
            self.invoke('prepare', 'A', '/fixture')
        self.root = 'dk2'
        with self.assertRaises(ValueError):
            self.invoke('prepare', 'A', '/fixture')
        self.prepare.assert_not_called()
        self.commands.assert_not_called()

    def test_arm_and_confirm_only_the_prepared_receipt(self):
        with self.assertRaises(ValueError):
            self.invoke('arm', 'B')
        slot.record_prepared('B', self.receipt)
        self.assertEqual(slot.prepared(), {'slot': 'B', 'sha256': hashlib.sha256(self.receipt).hexdigest()})
        self.invoke('arm', 'B')
        self.assertEqual(slot.marker(slot.NEXT), 'B')
        self.assertEqual(slot.marker(slot.GOOD), 'A')
        self.root = 'dk2'
        self.chosen = 'emberbsd,trial-watchdog'
        slot.write_marker(slot.NEXT, '-')
        (self.state / 'release.json').write_bytes(self.receipt + b' ')
        with self.assertRaises(ValueError):
            self.invoke('confirm', 'B')
        self.assertEqual(slot.marker(slot.GOOD), 'A')
        self.commands.assert_not_called()
        (self.state / 'release.json').write_bytes(self.receipt)
        with patch.object(self, 'watchdog', 'testwdt0, 16 second period [disarmed]'):
            with self.assertRaises(ValueError):
                self.invoke('confirm', 'B')
        self.commands.assert_not_called()
        self.invoke('confirm', 'B')
        self.assertEqual(slot.marker(slot.GOOD), 'B')
        self.assertEqual(slot.marker(slot.BACK), 'B')
        self.assertIn(call('/sbin/wdogctl', '-d'), self.commands.call_args_list)

    def test_cancel_requires_the_requested_inactive_slot_and_readback(self):
        slot.record_prepared('B', self.receipt)
        self.invoke('arm', 'B')
        with self.assertRaises(ValueError):
            self.invoke('cancel', 'A')
        self.assertEqual(slot.marker(slot.NEXT), 'B')
        self.invoke('cancel', 'B')
        self.assertEqual((self.boot / slot.NEXT).read_bytes(), b'-')
        self.assertEqual(slot.marker(slot.GOOD), 'A')
        self.commands.assert_not_called()

    def test_primary_commit_failure_rearms_watchdog_and_requests_reset(self):
        self.root = 'dk2'
        self.chosen = 'emberbsd,trial-watchdog'
        slot.record_prepared('B', self.receipt)
        for rearm_fails in (False, True):
            with self.subTest(rearm_fails=rearm_fails):
                self.commands.reset_mock()
                self.commands.side_effect = [None, OSError('watchdog') if rearm_fails else None, None]
                with patch.object(slot, 'write_marker', side_effect=OSError('FAT write')) as write:
                    with self.assertRaises(OSError):
                        self.invoke('confirm', 'B')
                write.assert_called_once_with(slot.GOOD, 'B')
                self.assertEqual(self.commands.call_args_list, [
                    call('/sbin/wdogctl', '-d'), call('/sbin/wdogctl', '-e', '-p', '16', 'testwdt0'),
                    call('/sbin/shutdown', '-r', 'now')])
                self.assertEqual(slot.marker(slot.GOOD), 'A')

    def test_repairs_secondary_commit_before_reusing_peer(self):
        self.root = 'dk2'
        slot.write_marker(slot.GOOD, 'B')
        self.prepare.side_effect = lambda *args: self.assertEqual(slot.marker(slot.BACK), 'B')
        self.invoke('prepare', 'A', '/fixture')
        self.prepare.assert_called_once()


class DefaultsTests(unittest.TestCase):
    def setUp(self):
        self.work = Path(self.enterContext(tempfile.TemporaryDirectory())).resolve()
        self.root = self.work / 'target'
        (self.root / 'etc').mkdir(parents=True)

    def archive(self, name, entries, *, gid=None, dirs=()):
        path = self.work / (name + '.tar.xz')
        with tarfile.open(path, 'w:xz') as tar:
            for directory, mode in dirs:
                item = tarfile.TarInfo('./etc/' + directory)
                item.type = tarfile.DIRTYPE
                item.mode = mode
                item.uid = os.getuid()
                item.gid = os.getgid()
                tar.addfile(item)
            for name, data in entries.items():
                item = tarfile.TarInfo('./etc/' + name)
                item.mode = 0o640
                item.uid = os.getuid()
                item.gid = os.getgid() if gid is None else gid
                item.size = len(data)
                tar.addfile(item, io.BytesIO(data))
        return path

    def install(self, entries):
        for name, data in entries.items():
            path = self.root / 'etc' / name
            path.write_bytes(data)
            path.chmod(0o640)

    def test_merge_new_changed_removed_and_locally_edited_defaults(self):
        tar = '/bin/tar' if sys.platform.startswith('netbsd') else shutil.which('bsdtar')
        if not tar:
            self.skipTest('select bsdtar for native set extraction')
        before = dict(local=b'old-local', change=b'old', obsolete=b'remove')
        new = dict(local=b'old-local', change=b'new', add=b'addition')
        self.install(dict(before, local=b'site setting'))

        def extract(program, *args, **kwargs):
            self.assertEqual(program, '/bin/tar')
            self.assertEqual(args[args.index('-C') + 1], self.root)
            subprocess.run([tar, *args], check=True, **kwargs)

        with patch.object(slot, 'run', side_effect=extract):
            slot.merge_defaults(self.root, self.archive('old', before), self.archive('new', new))
        self.assertEqual({p.name: p.read_bytes() for p in (self.root / 'etc').iterdir()},
                         dict(local=b'site setting', change=b'new', add=b'addition'))

    def test_overlapping_local_change_stops_before_installing_that_file(self):
        self.install({'site': b'local'})
        with self.assertRaises(ValueError), patch.object(slot, 'run') as run:
            slot.merge_defaults(self.root, self.archive('old', {'site': b'initial'}),
                                self.archive('new', {'site': b'upstream'}))
        self.assertEqual((self.root / 'etc/site').read_bytes(), b'local')
        run.assert_not_called()

    def test_merge_directory_defaults_and_retain_site_files(self):
        before = [('site', 0o750), ('obsolete', 0o700), ('mode', 0o750)]
        after = [('private', 0o700), ('private/empty', 0o710), ('mode', 0o711)]
        for name, mode in before:
            path = self.root / 'etc' / name
            path.mkdir()
            path.chmod(mode)
        local = self.root / 'etc/site/local'
        local.write_bytes(b'site')
        with patch.object(slot, 'run') as extract:
            slot.merge_defaults(self.root, self.archive('old', {}, dirs=before),
                                self.archive('new', {}, dirs=after))
            extract.assert_not_called()
        self.assertEqual(local.read_bytes(), b'site')
        self.assertFalse((self.root / 'etc/obsolete').exists())
        for name, mode in after:
            path = self.root / 'etc' / name
            self.assertTrue(path.is_dir())
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)

    def test_native_archive_mode_matches_installed_regular_file(self):
        self.install({'site': b'default'})
        item = tarfile.TarInfo('./etc/site')
        # Native set producers include the inode type in the tar mode field.
        item.mode = 0o100640
        item.uid = os.getuid()
        item.gid = os.getgid()
        tar = Mock()
        tar.extractfile.return_value = io.BytesIO(b'default')
        self.assertEqual(slot.member_id(item, tar), slot.installed_id(self.root, Path('etc/site')))

    def test_changed_upstream_or_local_group_participates_in_merge(self):
        files = {'site': b'default'}
        self.install(files)
        old = self.archive('old', files)
        changed_group = self.archive('group', files, gid=os.getgid() + 1)
        with patch.object(slot, 'run') as extract:
            slot.merge_defaults(self.root, old, changed_group)
            extract.assert_called_once()
        changed_content = self.archive('content', {'site': b'next'}, gid=os.getgid() + 1)
        with patch.object(slot, 'run') as extract, self.assertRaises(ValueError):
            slot.merge_defaults(self.root, changed_group, changed_content)
        extract.assert_not_called()
        self.assertEqual((self.root / 'etc/site').read_bytes(), b'default')


class LoaderTests(unittest.TestCase):
    # Run the loader conditionals unmodified. Model the byte in RAM, the FAT
    # records, and the transfer to the board script with shell functions.
    model = r"""
setenv() {
    if [ "$#" -eq 1 ]; then unset "$1"; else export "$1=$2"; fi
}
mw_b() { memory=$2; }
setexpr_b() { export "$1=$memory"; }
fatload() {
    case "${4##*/}" in
        osgood.dat) memory=$good ;;
        osback.dat) memory=$back ;;
        osnext.dat)
            if [ "$wrote" = yes ] && [ "$fault" = read ]; then return 1; fi
            memory=$next ;;
        *) exit 97 ;;
    esac
    [ "$memory" != missing ] || return 1
    filesize=1
    if [ "$memory" = oversized ]; then filesize=2; fi
}
fatwrite() {
    [ "${4##*/}" = osnext.dat ] && [ "$5" = 1 ] || exit 97
    [ "$fault" != write ] || return 1
    next=$memory
    wrote=yes
    if [ "$fault" = badbyte ]; then next=41; fi
}
load() { [ "$4" = slotboot.scr ] && [ "$handoff" != missing ]; }
launch() {
    echo "boot $ember_slot $ember_trial $next"
    if [ "$handoff" = boot ]; then exit 0; fi
}
reset() { echo "reset"; exit 0; }
devtype=mmc
devnum=0
distro_bootpart=1
"""

    def execute(self, *, good='41', back='41', next='42', fault='', handoff='boot', board=False):
        script = (IMAGE.parent / 'boot/uboot-ffs-slots.cmd').read_text()
        script = script.replace('setexpr.b ', 'setexpr_b ').replace('mw.b ', 'mw_b ')
        script = script.replace('source 0x43200000', 'launch 0x43200000')
        commands = self.model
        recovery = 'echo recovery'
        if board:
            # A sourced U-Boot script returns to the outer script on "exit".
            # Model its commands in the same shell to retain environment changes.
            board_script = (IMAGE.parent / 'boot/orangepi-zero4-boot.cmd').read_text()
            board_script = board_script.replace('mw.l ', 'mw_l ').replace('setexpr.l ', 'setexpr_l ')
            board_script = board_script.replace('exit 1', 'return 1')
            commands += '''
prefix=/
mw_l() { :; }
sleep() { :; }
setexpr_l() {
    case "$1" in
        pins_up) pins_up=7fff ;;
        pins_down) pins_down=0 ;;
        mode) mode=1 ;;
        config) config=1 ;;
        *) exit 97 ;;
    esac
}
load() {
    echo "load $4"
    [ "$4" != /os-b/netbsd.img ] || [ "$handoff" != missing ]
}
fdt() { :; }
booti() { echo "$bootargs $ember_trial"; exit 0; }
launch() {
''' + board_script + '\n}\n'
            recovery = 'echo "recovery ${prefix}boot.scr"'
        return subprocess.run(['/bin/sh', '-c', commands + script + '\n' + recovery],
                              env=dict(good=good, back=back, next=next, fault=fault, handoff=handoff),
                              text=True, capture_output=True)

    def test_consumes_trial_before_boot_and_uses_confirmed_after_io_errors(self):
        for fault, result in [('', 'boot B yes 2d'), ('write', 'boot A no 42'),
                              ('read', 'boot A no 2d'), ('badbyte', 'boot A no 41')]:
            with self.subTest(fault=fault):
                completed = self.execute(fault=fault)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(completed.stdout.splitlines(), [result])

    def test_fallback_marker_and_loader_return(self):
        for invalid in ['missing', 'oversized', 'ff']:
            with self.subTest(invalid=invalid):
                result = self.execute(good=invalid, back='42', next='2d')
                self.assertEqual(result.stdout.splitlines(), ['boot B no 2d'])
                result = self.execute(good=invalid, back=invalid)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')
        for handoff in ['missing', 'returns']:
            with self.subTest(handoff=handoff):
                trial = self.execute(handoff=handoff)
                self.assertEqual(trial.stdout.splitlines()[-1], 'reset')
                stable = self.execute(handoff=handoff, next='2d')
                self.assertEqual(stable.stdout.splitlines()[-1], 'recovery')

    def test_board_selects_matching_kernel_and_keeps_the_recovery_entry(self):
        expected = {'41': ('/', 'netbsd-root'), '42': ('/os-b/', 'ember-root-b')}
        for marker, (prefix, root) in expected.items():
            with self.subTest(root=root):
                stable = self.execute(good=marker, next='2d', board=True)
                self.assertEqual(stable.returncode, 0, stable.stderr)
                self.assertEqual(stable.stdout.splitlines(), [
                    'load /slotboot.scr',
                    f'load {prefix}dtb/allwinner/sun60i-a733-orangepi-zero3w.dtb',
                    f'load {prefix}netbsd.img', f'root=NAME={root} no'])
        fallback = self.execute(good='42', next='2d', board=True, handoff='missing')
        self.assertEqual(fallback.stdout.splitlines()[-1], 'recovery /boot.scr')


if __name__ == '__main__':
    unittest.main()
