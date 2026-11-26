#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the U-Boot FFS selector against a model of the FAT boot commands."""

from pathlib import Path
import subprocess
import unittest

BOOT = Path(__file__).resolve().parent


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
    if [ "$memory" = oversized ] || { [ "$wrote" = yes ] && [ "$fault" = long ]; }; then filesize=2; fi
}
fatwrite() {
    [ "${4##*/}" = osnext.dat ] && [ "$5" = 1 ] || exit 97
    [ "$fault" != write ] || return 1
    [ "$wrote" != yes ] || exit 98
    wrote=yes
    # On this vendor command, a prefixed root path can report a successful
    # write although the next read still returns the original byte.
    if [ "$4" = /osnext.dat ]; then return 0; fi
    next=$memory
    if [ "$fault" = badbyte ]; then next=41; fi
}
load() { [ "$4" = /slotboot.scr ] && [ "$handoff" != missing ]; }
launch() {
    echo "boot $ember_slot $ember_trial $next"
    if [ "$handoff" = boot ]; then exit 0; fi
}
reset() { echo "reset"; exit 0; }
prefix=/
devtype=mmc
devnum=0
distro_bootpart=1
"""

    def execute(self, *, good='41', back='41', next='42', fault='', handoff='boot', board=False):
        script = (BOOT / 'uboot-ffs-slots.cmd').read_text()
        script = script.replace('setexpr.b ', 'setexpr_b ').replace('mw.b ', 'mw_b ')
        script = script.replace('@MARKER_ADDRESS@', '0x1000').replace('@SCRIPT_ADDRESS@', '0x2000')
        script = script.replace('source 0x2000', 'launch 0x2000')
        script = script.replace('@ROOT_A@', 'netbsd-root').replace('@ROOT_B@', 'ember-root-b')
        commands = self.model
        recovery = 'echo recovery'
        if board:
            # A sourced U-Boot script returns to the outer script on "exit".
            # Model its commands in the same shell to retain environment changes.
            board_script = (BOOT / 'orangepi-zero4-boot.cmd').read_text()
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
                              ('read', 'boot A no 2d'), ('long', 'boot A no 2d'), ('badbyte', 'boot A no 41')]:
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

    def test_vendor_root_leaf_write_hands_off_a_single_consumed_trial(self):
        boot = self.execute(board=True)
        self.assertEqual(boot.returncode, 0, boot.stderr)
        self.assertEqual(boot.stdout.splitlines()[-1], 'root=NAME=ember-root-b yes')

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
