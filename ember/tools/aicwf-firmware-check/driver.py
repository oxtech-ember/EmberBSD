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
import patched
import patching
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


def symbols(prefix, work, process, patch=False):
    data = process.capture([str(prefix / 'bin/arm-none-eabi-nm'), '-n', str(work / 'harness.elf')], env=process.sanitized_environment()).stdout.decode()
    selected = {}
    required = {'mailbox', 'results', 'harness_done'} | ({'frame_results'} if patch else set())
    for line in data.splitlines():
        words = line.split()
        if len(words) == 3 and words[2] in required:
            if words[2] in selected:
                raise ValueError('Repeated fixture symbol')
            selected[words[2]] = int(words[0], 16)
    if set(selected) != required:
        raise ValueError('Required fixture symbol missing')
    return selected


def execute(qemu, work, addresses, timeout, process, jsonio, expected_hashes, patch=None, rows=None):
    if not qemu.is_absolute() or not qemu.is_file() or not os.access(qemu, os.X_OK):
        raise ValueError('Explicit host QEMU executable required')
    if set(expected_hashes) != set(policy.SPANS):
        raise ValueError('Exactly nine expected loaded hashes are required')
    pending = work / 'ownership-pending.json'
    env = process.sanitized_environment()
    raw, error, closure = None, None, None
    rows = cases.inputs() if rows is None else rows
    frames = None
    temporary = tempfile.mkdtemp(prefix='aic-gdb-', dir=work.parent)
    path = Path(temporary) / 'gdb'
    if len(str(path).encode()) >= 100 or ',' in str(path):
        Path(temporary).rmdir()
        raise ValueError('Private Unix socket path is too long or contains a comma')
    argv = [str(qemu), '-M', 'mps2-an386', '-accel', 'tcg', '-display', 'none', '-serial', 'none', '-monitor', 'none', '-nic', 'none', '-no-reboot', '-kernel', str(work / 'harness.elf'), '-chardev', 'socket,id=gdb0,path=' + str(path) + ',server=on,wait=off', '-gdb', 'chardev:gdb0', '-S']
    jsonio.write_json(work / 'launch.json', dict(argv=argv, env_delta={}, full_firmware_boot=False, replacements=patch is not None, machine_scope='Cortex-M4 CPU model only'))
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
            loaded_before = {}
            for name, (start, end) in policy.SPANS.items():
                loaded_before[name] = hashlib.sha256(remote.memory(start, end - start)).hexdigest()
                if loaded_before[name] != expected_hashes[name]:
                    raise ValueError('Loaded original instruction slice differs: ' + name)
            jsonio.write_json(work / 'loaded-expected.json', expected_hashes, mode=0o600)
            jsonio.write_json(work / 'loaded-before.json', loaded_before, mode=0o600)
            stopped = remote.command('c')
            if not stopped.startswith((b'T05', b'S05')):
                raise ValueError('Fixture did not reach a normal breakpoint')
            header = remote.memory(addresses['mailbox'], 16)
            (work / 'mailbox.bin').write_bytes(header)
            magic, count, stride, state = struct.unpack('<4I', header)
            if (magic, count, stride, state) != (0x55303242, len(rows), observations.RECORD_BYTES, 2):
                raise ValueError('Incomplete CPU fixture mailbox')
            raw = remote.memory(addresses['results'], count * stride)
            if patch is not None:
                frames = remote.memory(addresses['frame_results'], count * patched.FRAME_BYTES)
            loaded_after = {}
            for name, (start, end) in policy.SPANS.items():
                loaded_after[name] = hashlib.sha256(remote.memory(start, end - start)).hexdigest()
                if loaded_after[name] != loaded_before[name]:
                    raise ValueError('Selected instruction bytes changed during execution')
            jsonio.write_json(work / 'loaded-after.json', loaded_after, mode=0o600)
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
    if patch is not None:
        (work / 'frame-results.bin').write_bytes(frames)
        return patched.compare(rows, raw, frames)
    return observations.compare(rows, raw)


def main(argv):
    args = policy.arguments(argv)
    work = args.output
    if args.exercise != 'functional':
        raise ValueError('IRQ observation requires the next reviewed source stage')
    patch = None if args.patch_elf is None else patching.load(args.patch_elf, args.patch_manifest, args.causal_early_enable)
    rows = None if patch is None else patched.inputs()
    if work.exists() or work.is_symlink():
        raise ValueError('Output already exists')
    process = jsonio = prefix = None
    if args.mode != 'prepare':
        process, jsonio = sdk(args.sdk_source)
        prefix = toolchain(args.compiler_prefix, process)
    if patch is None:
        prepared = policy.prepare(args.firmware, work)
    else:
        prepared = policy.prepare(args.firmware, work, patch, rows)
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
            state = execute(args.qemu, work, symbols(prefix, work, process, patch is not None), args.timeout, process, jsonio, {row['name']: row['sha256'] for row in prepared}, patch, rows)
    except BaseException as error:
        state.update(state='refused', accepted_original_fixture=False, accepted_secure_firmware=False, error=str(error))
        try:
            jsonio.write_json(work / 'status.json', state, exclusive=False, mode=0o600)
        except BaseException as secondary:
            raise RuntimeError('Primary failure: ' + str(error) + '; status write also failed: ' + str(secondary)) from error
        raise
    jsonio.write_json(work / 'status.json', state, exclusive=False, mode=0o600)
    return state
