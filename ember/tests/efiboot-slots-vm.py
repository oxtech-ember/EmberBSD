#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test the installed EFI binary on a disposable FAT disk in QEMU."""

import argparse
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
import time


def run(*command, **args):
    return subprocess.run(command, check=True, **args)


def put(disk, path, source):
    run('mcopy', '-o', '-i', disk, source, '::/' + path)


def boot(args, disk, nvram, expected):
    machine = 'virt' if args.arch == 'aarch64' else 'q35'
    cpu = 'cortex-a72' if args.arch == 'aarch64' else 'max'
    command = [args.qemu, '-machine', machine, '-cpu', cpu, '-accel', 'tcg',
               '-m', '512', '-smp', '1', '-display', 'none', '-monitor', 'none',
               '-serial', 'stdio', '-net', 'none', '-no-reboot',
               '-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={args.code}',
               '-drive', f'if=pflash,unit=1,format=raw,file={nvram}',
               '-drive', f'if=none,format=raw,id=esp,file={disk}',
               '-device', 'virtio-blk-pci,drive=esp']
    log = bytearray()
    deadline = time.monotonic() + args.timeout
    with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT) as process:
        try:
            with selectors.DefaultSelector() as serial:
                serial.register(process.stdout, selectors.EVENT_READ)
                while time.monotonic() < deadline:
                    if not serial.select(max(0, deadline - time.monotonic())):
                        break
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    log.extend(chunk)
                    if all(text.encode() in log for text in expected):
                        return
            raise RuntimeError(f'expected {expected} on EFI console:\n' +
                               bytes(log[-6000:]).decode(errors='replace'))
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', required=True, choices=('aarch64', 'x86_64'))
    parser.add_argument('--loader', required=True, type=Path, help='production efiboot')
    parser.add_argument('--test-loader', required=True, type=Path, help='EFIBOOT_SLOT_TEST=yes efiboot')
    parser.add_argument('--code', required=True, type=Path)
    parser.add_argument('--vars', required=True, type=Path)
    parser.add_argument('--qemu', help='QEMU system emulator for the selected architecture')
    parser.add_argument('--timeout', type=int, default=75)
    args = parser.parse_args()
    args.qemu = args.qemu or 'qemu-system-' + args.arch
    with tempfile.TemporaryDirectory() as workdir:
        work = Path(workdir)
        disk = work / 'esp.img'
        with disk.open('wb') as image:
            image.truncate(64 << 20)
        run('mformat', '-i', disk, '-F', '::')
        for subdir in ('EFI', 'EFI/BOOT', 'EFI/EmberBSD'):
            run('mmd', '-i', disk, '::/' + subdir)
        loader = 'BOOTAA64.EFI' if args.arch == 'aarch64' else 'BOOTX64.EFI'
        put(disk, 'EFI/BOOT/' + loader, args.loader)
        for slot in ('A', 'B'):
            name = 'boot-' + slot.lower() + '.cfg'
            (work / name).write_text(f'banner=Configuration for slot {slot}\n'
                                     'menu=Boot prompt:prompt\ndefault=1\ntimeout=0\n')
            put(disk, 'EFI/EmberBSD/' + name, work / name)
        nvram = work / 'vars.fd'
        shutil.copyfile(args.vars, nvram)
        for name, value in [('osgood.dat', b'A'), ('osback.dat', b'A'), ('osnext.dat', b'B'),
                            ('manual-test', b'1')]:
            (work / name).write_bytes(value)
            put(disk, 'EFI/EmberBSD/' + name, work / name)
        # Test markers cannot enable a production loader on an unqualified VM.
        boot(args, disk, nvram, ['EFI confirmed slot A', 'Configuration for slot A'])
        if subprocess.check_output(['mtype', '-i', disk, '::/EFI/EmberBSD/osnext.dat']) != b'-':
            raise RuntimeError('production firmware retained an unsupported trial')
        put(disk, 'EFI/BOOT/' + loader, args.test_loader)
        put(disk, 'EFI/EmberBSD/osnext.dat', work / 'osnext.dat')
        boot(args, disk, nvram, ['EFI trial slot B', 'Configuration for slot B'])
        next_marker = subprocess.check_output(['mtype', '-i', disk, '::/EFI/EmberBSD/osnext.dat'])
        if next_marker != b'-':
            raise RuntimeError('the candidate entry was not consumed on the FAT volume')
        boot(args, disk, nvram, ['EFI confirmed slot A', 'Configuration for slot A'])
        # A pending managed update on a machine without a reset provider returns to A.
        (work / 'osplan.dat').write_text('B ' + '0' * 64 + '\n')
        put(disk, 'EFI/EmberBSD/osplan.dat', work / 'osplan.dat')
        (work / 'osnext.dat').write_bytes(b'B')
        put(disk, 'EFI/EmberBSD/osnext.dat', work / 'osnext.dat')
        boot(args, disk, nvram, ['EFI confirmed slot A', 'Configuration for slot A'])
        if subprocess.check_output(['mtype', '-i', disk, '::/EFI/EmberBSD/osnext.dat']) != b'-':
            raise RuntimeError('unsupported managed trial was not consumed')
        run('mdel', '-i', disk, '::/EFI/EmberBSD/osplan.dat')
        # The one-shot also resets after a missing kernel, before ExitBootServices.
        (work / 'boot-b.cfg').write_text('menu=Boot missing kernel:boot absent-kernel\ndefault=1\ntimeout=0\n')
        put(disk, 'EFI/EmberBSD/boot-b.cfg', work / 'boot-b.cfg')
        put(disk, 'EFI/EmberBSD/osnext.dat', work / 'osnext.dat')
        boot(args, disk, nvram, ['EFI trial slot B', 'EFI trial kernel did not start; restarting'])
        boot(args, disk, nvram, ['EFI confirmed slot A', 'Configuration for slot A'])
        # The secondary confirmed record also works after an invalid primary.
        (work / 'osgood.dat').write_bytes(b'?')
        put(disk, 'EFI/EmberBSD/osgood.dat', work / 'osgood.dat')
        boot(args, disk, nvram, ['EFI confirmed slot A', 'Configuration for slot A'])
    print(f'{args.arch}: candidate consumed, confirmed slot recovered, backup record recovered')


if __name__ == '__main__':
    main()
