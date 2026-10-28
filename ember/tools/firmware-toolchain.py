#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Select native and freestanding firmware compilers through the SDK."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def validate_gcc_tools(prefix, cross, host, search_path, validate, capture):
    """Validate selected target GCC and every compiler driver component."""
    if not cross or not os.path.isabs(cross) or not cross.startswith(os.path.join(prefix, "bin") + os.sep):
        raise ValueError("select CROSS_COMPILE inside target prefix/bin")
    env = {"PATH": os.path.dirname(cross) + os.pathsep + search_path}
    def check(path):
        return validate(prefix, path, host, search_path=env["PATH"])
    tools = {name: cross + name for name in ("gcc", "gcc-ar", "as", "ld", "objcopy", "objdump")}
    for path in tools.values():
        check(path)
    for name in ("cc1", "collect2", "lto1", "lto-wrapper", "as", "ld"):
        path = capture([tools["gcc"], "-print-prog-name=" + name], env=env).stdout.decode("utf-8", "strict").strip()
        if not os.path.isabs(path):
            if path != name:
                raise ValueError("invalid GCC driver component: " + name)
            path = shutil.which(path, path=env["PATH"])
            if not path:
                raise ValueError("missing GCC driver component: " + name)
        check(path)
        if name in ("as", "ld") and os.path.realpath(path) != os.path.realpath(tools[name]):
            raise ValueError("GCC selects a different " + name)
    return tools


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-source', required=True)
    parser.add_argument('--host-prefix', required=True)
    parser.add_argument('--target-prefix', required=True)
    parser.add_argument('--host-family', choices=('clang', 'gcc'), default='clang')
    parser.add_argument('--target-family', choices=('clang', 'gcc'), default='clang')
    parser.add_argument('--host-role', action='append', default=[])
    parser.add_argument('--run', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    sdk = Path(args.sdk_source)
    if not sdk.is_absolute() or not Path(args.host_prefix).is_absolute() or not Path(args.target_prefix).is_absolute():
        parser.error('SDK source and prefixes must be absolute')
    sys.path.insert(0, str(sdk / 'src'))
    from emberbsd_sdk import host_prefix, native_checks, process, stream, target_toolchain
    from emberbsd_sdk.errors import CommandError, ProcessOwnershipError, SDKError
    try:
        roles = {'cc', 'cxx', 'cpp', 'sh', 'python', *args.host_role}
        if args.run:
            roles |= {'bash', 'gmake', 'ruby', 'patch', 'iasl', 'dtc', 'shasum', 'awk',
                      'ar', 'objcopy', 'objdump', 'gpatch'}
        native = host_prefix.load_host_prefix(args.host_prefix, compiler_family=args.host_family,
                                             required_roles=roles)
        target = (target_toolchain.clang_compiler(args.target_prefix, 'aarch64-unknown-none-elf',
                   host=dict(native.host), env={'PATH': native.path}) if args.target_family == 'clang' else None)
        if args.target_family == 'gcc':
            validate_gcc_tools(args.target_prefix, os.environ.get('CROSS_COMPILE'), dict(native.host),
                               native.path, host_prefix.validate_host_executable, process.capture)
        if not args.run:
            print(json.dumps({'host': native.as_dict(), 'target': target}, sort_keys=True))
            return 0
        command = args.command[1:] if args.command[:1] == ['--'] else args.command
        if not command or not os.path.isabs(command[0]):
            raise SDKError('firmware builder path must be absolute')
        git = os.environ.get('EMBER_GIT') or shutil.which('git')
        if not git or not os.path.isabs(git) or not os.access(git, os.X_OK):
            raise SDKError('select an executable source Git with EMBER_GIT')
        try:
            subprocess.run([native.tool('python'), '-B', '-c', 'import elftools'],
                           env={'PATH': native.path}, check=True, capture_output=True)
        except subprocess.CalledProcessError as exc:
            raise SDKError('selected native Python is missing pyelftools') from exc
        codec_env = {}
        for name in ('EMBER_PORTS_SOURCE', 'EMBER_7ZIP_SOURCE', 'EMBER_BROTLI_SOURCE'):
            value = os.environ.get(name, '')
            if not os.path.isabs(value) or not os.path.isdir(value):
                raise SDKError('select an absolute directory for ' + name)
            codec_env[name] = value
        verify_env = {'PATH': native.path}
        if os.environ.get('TMPDIR'):
            verify_env['TMPDIR'] = os.environ['TMPDIR']
        for package, key in (('7zip', 'EMBER_7ZIP_SOURCE'), ('brotli', 'EMBER_BROTLI_SOURCE')):
            owner = os.path.join(codec_env['EMBER_PORTS_SOURCE'], 'scripts/source-inputs', package, 'source.py')
            verify_command = [native.tool('python'), '-B', owner, '--sdk-source', str(sdk),
                       '--git', git, 'verify-source', '--source', codec_env[key],
                       '--patch', native.tool('gpatch')]
            archive = os.environ.get('EMBER_' + package.upper() + '_ARCHIVE')
            if archive:
                if not os.path.isabs(archive):
                    raise SDKError('codec archive path must be absolute')
                verify_command += ['--archive', archive]
            record = json.loads(process.capture(verify_command, env=verify_env).stdout)
            tree = record.get('tree_sha256')
            if not isinstance(tree, str) or not re.fullmatch(r'[0-9a-f]{64}', tree):
                raise SDKError('invalid prepared codec identity')
            codec_env['EMBER_' + package.upper() + '_TREE'] = tree
        tmp = tempfile.mkdtemp(prefix='firmware-tools-', dir=os.environ.get('TMPDIR'))
        keep = False
        try:
            bindings = os.path.join(tmp, 'bindings')
            env = native_checks.check_environment(native, bindings=bindings)
            os.symlink(git, os.path.join(bindings, 'git'))
            os.symlink(native.tool('gmake'), os.path.join(bindings, 'make'))
            env.update(codec_env)
            env.update(HOSTCC=env['CC'], HOSTCPP=env['CPP'], HOSTAS=env['CC'],
                       HOSTLD=env['CC'], HOSTAR=native.tool('ar'),
                       HOSTOC=native.tool('objcopy'), HOSTOD=native.tool('objdump'),
                       HOSTDTC=native.tool('dtc'))
            env.update(EMBERBSD_SDK_SOURCE=str(sdk),
                       EMBER_HOST_PREFIX=args.host_prefix, EMBER_TARGET_PREFIX=args.target_prefix,
                       EMBER_HOST_COMPILER_FAMILY=args.host_family,
                       EMBER_TARGET_COMPILER_FAMILY=args.target_family,
                       RPI_BUILD_MAKE=native.tool('gmake'),
                       CLANG_HOST_BIN=bindings + '/',
                       PYTHON_COMMAND=native.tool('python'),
                       IASL_PREFIX=os.path.dirname(native.tool('iasl')) + '/')
            if target is not None:
                env.update(EMBER_FIRMWARE_CC=target['binaries']['cc']['selected_path'],
                           EMBER_FIRMWARE_LLD=target['binaries']['ld']['selected_path'],
                           EMBER_FIRMWARE_AR=target['binaries']['ar']['selected_path'],
                           EMBER_FIRMWARE_OC=target['binaries']['objcopy']['selected_path'],
                           EMBER_FIRMWARE_OD=target['binaries']['objdump']['selected_path'],
                           EMBER_FIRMWARE_INCLUDE=target['resource_dir'] + '/include')
            for name in ('CROSS_COMPILE', 'BUILD_JOBS'):
                if name in os.environ:
                    env[name] = os.environ[name]
            def forward(reader):
                while chunk := reader.read(65536):
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
            try:
                result = stream.run_streamed([native.tool('bash'), *command], forward, env=env,
                                             empty_snapshot_timeout=120)
                if result.stderr:
                    sys.stderr.buffer.write(result.stderr)
                return 0
            except ProcessOwnershipError:
                keep = True
                print('firmware-toolchain: preserved active bindings ' + tmp, file=sys.stderr)
                raise
            except CommandError as exc:
                if exc.stderr:
                    sys.stderr.buffer.write(exc.stderr)
                return exc.returncode if exc.returncode >= 0 else 128 - exc.returncode
        finally:
            if not keep:
                shutil.rmtree(tmp)
    except (SDKError, OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, 'firmware-toolchain: ' + str(exc) + '\n')


if __name__ == '__main__':
    sys.exit(main())
