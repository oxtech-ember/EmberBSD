# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; explicit SDK API1 build and CPU-fixture lifecycle.
import hashlib
import importlib
import json
import os
from pathlib import Path
import signal
import shutil
import struct
import sys
import tempfile
import time

import cases
import observations
import policy
from remote import Remote

HERE = Path(__file__).resolve().parent


def sdk(source):
    if source is None or not source.is_absolute():
        raise ValueError('An absolute SDK API1 source is required')
    root = source.resolve(strict=True)
    sys.path.insert(0, str(root / 'src'))
    module = importlib.import_module('emberbsd_sdk')
    if type(module.API_VERSION) is not int or module.API_VERSION != 1 or Path(module.__file__).resolve().parent != root / 'src/emberbsd_sdk':
        raise ValueError('Selected SDK API1 source mismatch')
    return importlib.import_module('emberbsd_sdk.process'), importlib.import_module('emberbsd_sdk.jsonio')


def toolchain(prefix, process):
    prefix = prefix.resolve(strict=True)
    gcc = prefix / 'bin/arm-none-eabi-gcc'
    env = process.sanitized_environment()
    for option, expected in (('-dumpmachine', 'arm-none-eabi'), ('-dumpfullversion', '16.2.0')):
        value = process.capture([str(gcc), option], env=env).stdout.decode().strip()
        if value != expected:
            raise ValueError('ARM GCC16 compiler identity mismatch')
    return prefix


def symbols(prefix, work, process):
    data = process.capture([str(prefix / 'bin/arm-none-eabi-nm'), '-n', str(work / 'harness.elf')], env=process.sanitized_environment()).stdout.decode()
    selected = {}
    for line in data.splitlines():
        words = line.split()
        if len(words) == 3 and words[2] in ('mailbox', 'results', 'harness_done'):
            if words[2] in selected:
                raise ValueError('Repeated fixture symbol')
            selected[words[2]] = int(words[0], 16)
    if set(selected) != {'mailbox', 'results', 'harness_done'}:
        raise ValueError('Required fixture symbol missing')
    return selected


def execute(qemu, work, addresses, timeout, process, jsonio):
    if not qemu.is_absolute() or not qemu.is_file() or not os.access(qemu, os.X_OK):
        raise ValueError('Explicit host QEMU executable required')
    pending = work / 'ownership-pending.json'
    env = process.sanitized_environment()
    raw, error, closure = None, None, None
    temporary = tempfile.mkdtemp(prefix='aic-gdb-', dir=work.parent)
    path = Path(temporary) / 'gdb'
    if len(str(path).encode()) >= 100 or ',' in str(path):
        Path(temporary).rmdir()
        raise ValueError('Private Unix socket path is too long or contains a comma')
    argv = [str(qemu), '-M', 'mps2-an386', '-accel', 'tcg', '-display', 'none', '-serial', 'none', '-monitor', 'none', '-nic', 'none', '-no-reboot', '-kernel', str(work / 'harness.elf'), '-chardev', 'socket,id=gdb0,path=' + str(path) + ',server=on,wait=off', '-gdb', 'chardev:gdb0', '-S']
    jsonio.write_json(work / 'launch.json', dict(argv=argv, env_delta={}, full_firmware_boot=False, replacements=False, machine_scope='Cortex-M4 CPU model only'))
    jsonio.write_json(pending, dict(state='ownership-pending', accepted=False, control_root=temporary))
    fd = os.open(work / 'qemu.log', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as log:
        owner = process.OwnedProcessGroup(argv, cwd=work, env=env, stdout=log, stderr=log)
        remote = None
        try:
            owner.start()
            jsonio.write_json(pending, dict(state='ownership-pending', accepted=False, session=owner.pid, control_root=temporary), exclusive=False, mode=0o600)
            remote = Remote(path, time.monotonic() + timeout, owner)
            remote.breakpoint(addresses['harness_done'] & ~1)
            for name, (start, end) in policy.SPANS.items():
                if hashlib.sha256(remote.memory(start, end - start)).hexdigest() != hashlib.sha256((work / (name + '.bin')).read_bytes()).hexdigest():
                    raise ValueError('Loaded original instruction slice differs: ' + name)
            stopped = remote.command('c')
            if not stopped.startswith((b'T05', b'S05')):
                raise ValueError('Fixture did not reach a normal breakpoint')
            header = remote.memory(addresses['mailbox'], 16)
            (work / 'mailbox.bin').write_bytes(header)
            magic, count, stride, state = struct.unpack('<4I', header)
            if (magic, count, stride, state) != (0x55303242, len(cases.inputs()), observations.RECORD_BYTES, 2):
                raise ValueError('Incomplete CPU fixture mailbox')
            raw = remote.memory(addresses['results'], count * stride)
            for name, (start, end) in policy.SPANS.items():
                if remote.memory(start, end - start) != (work / (name + '.bin')).read_bytes():
                    raise ValueError('Original instruction bytes changed during execution')
        except BaseException as caught:
            error = caught
        finally:
            mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGHUP, signal.SIGTERM})
            try:
                try:
                    if remote is not None:
                        remote.close()
                except BaseException as caught:
                    if error is None:
                        error = caught
                try:
                    owner.stop()
                except BaseException as caught:
                    closure = caught
                    jsonio.write_json(pending, dict(state='ownership-unproven', accepted=False, session=owner.pid, control_root=temporary, error=str(caught)), exclusive=False, mode=0o600)
                if closure is None:
                    pending.unlink()
                    shutil.rmtree(temporary)
            finally:
                signal.pthread_sigmask(signal.SIG_SETMASK, mask)
    if closure is not None:
        raise closure from error
    if error is not None:
        raise error
    (work / 'results.bin').write_bytes(raw)
    return observations.compare(cases.inputs(), raw)


def main(argv):
    args = policy.arguments(argv)
    work = args.output
    if work.exists() or work.is_symlink():
        raise ValueError('Output already exists')
    process = jsonio = prefix = None
    if args.mode != 'prepare':
        process, jsonio = sdk(args.sdk_source)
        prefix = toolchain(args.compiler_prefix, process)
    policy.prepare(args.firmware, work)
    if args.mode == 'prepare':
        return dict(state='source-prepared-only', accepted_original_fixture=False, accepted_secure_firmware=False)
    state = dict(state='build-pending', accepted_original_fixture=False, accepted_secure_firmware=False)
    jsonio.write_json(work / 'status.json', state)
    try:
        for index, command in enumerate(policy.build_commands(prefix, work, HERE)):
            process.run_logged(command, work / ('build-' + str(index) + '.log'), cwd=work, env=process.sanitized_environment())
        jsonio.write_json(work / 'elf-layout.json', policy.elf_layout(work / 'harness.elf'), mode=0o600)
        if args.mode == 'build':
            state['state'] = 'built-not-executed'
        else:
            state = execute(args.qemu, work, symbols(prefix, work, process), args.timeout, process, jsonio)
    except BaseException as error:
        state.update(state='refused', accepted_original_fixture=False, accepted_secure_firmware=False, error=str(error))
        try:
            jsonio.write_json(work / 'status.json', state, exclusive=False, mode=0o600)
        except BaseException as secondary:
            raise RuntimeError('Primary failure: ' + str(error) + '; status write also failed: ' + str(secondary)) from error
        raise
    jsonio.write_json(work / 'status.json', state, exclusive=False, mode=0o600)
    return state
