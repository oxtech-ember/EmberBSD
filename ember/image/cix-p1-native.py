#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Select the SDK native and target inputs for the CIX static userland."""
import os
import shutil
from pathlib import Path
import sys


def main():
    if len(sys.argv) != 5:
        raise SystemExit('usage: cix-p1-native.py SOURCE NEW_OUTPUT TOOLDIR TARGET_PREFIX')
    source, output, tools, compiler = map(Path, sys.argv[1:])
    required = ('EMBERBSD_SDK_SOURCE', 'EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE',
                'EMBER_HOST_BFD_CONFIG', 'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE',
                'EMBER_LIBARCHIVE_SOURCE', 'EMBER_LIBEVENT_SOURCE')
    missing = [key for key in required if not os.environ.get(key)]
    family = os.environ.get('EMBER_TARGET_COMPILER_FAMILY', 'clang')
    host_family = os.environ.get('EMBER_HOST_COMPILER_FAMILY', 'clang')
    if family not in ('clang', 'gcc') or host_family not in ('clang', 'gcc'):
        raise SystemExit('compiler family must be clang or gcc')
    if family == 'clang':
        missing += [key for key in ('EMBER_SDK_SYSROOT',) if not os.environ.get(key)]
    if missing:
        raise SystemExit('missing required inputs: ' + ', '.join(missing))
    sdk = Path(os.environ['EMBERBSD_SDK_SOURCE'])
    sys.path.insert(0, str(sdk / 'src'))
    try:
        from emberbsd_sdk import application_toolchain, host_prefix, jsonio, libedit, llvm_runtime, native_os, process, target_toolchain
        from emberbsd_sdk.errors import SDKError
    except ImportError as exc:
        raise SystemExit('selected SDK source lacks required APIs: ' + str(exc)) from exc
    try:
        if native_os.NATIVE_OS_API != 1 or application_toolchain.APPLICATION_TOOLCHAIN_API < 2:
            raise SDKError('selected SDK has an incompatible native OS or application API')
        inputs = (source, output, tools, compiler, sdk, Path(os.environ['EMBER_HOST_PREFIX']),
                  Path(os.environ['EMBER_LIBEDIT_SOURCE']), Path(os.environ['EMBER_HOST_BFD_CONFIG']),
                  Path(os.environ['EMBER_LLVM_SOURCE']), Path(os.environ['EMBER_PORTS_SOURCE']),
                  Path(os.environ['EMBER_LIBARCHIVE_SOURCE']), Path(os.environ['EMBER_LIBEVENT_SOURCE']))
        if family == 'clang':
            inputs += (Path(os.environ['EMBER_SDK_SYSROOT']),)
        if any(not p.is_absolute() or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./-' for c in str(p)) for p in inputs):
            raise SDKError('input paths must be absolute and contain no whitespace or shell metacharacters')
        if not source.is_dir() or not (source / 'build.sh').is_file():
            raise SDKError('source must contain build.sh')
        if not (Path(os.environ['EMBER_LIBEDIT_SOURCE']) / 'libedit-source.json').is_file() or not (Path(os.environ['EMBER_LIBEDIT_SOURCE']) / 'ports.distinfo').is_file():
            raise SDKError('EMBER_LIBEDIT_SOURCE is incomplete')
        if not Path(os.environ['EMBER_HOST_BFD_CONFIG']).is_file():
            raise SDKError('EMBER_HOST_BFD_CONFIG must name the selected host BFD configuration')
        if not (Path(os.environ['EMBER_LLVM_SOURCE']) / 'runtimes/CMakeLists.txt').is_file():
            raise SDKError('EMBER_LLVM_SOURCE must name the prepared LLVM source')
        for name in ('nbmake', 'nbmkesdb', 'nbmkcsmapper'):
            if not os.access(tools / 'bin' / name, os.X_OK):
                raise SDKError('missing selected OS host tool: ' + name)
        if output.exists() or output.is_symlink():
            raise SDKError('output must not already exist')
        native = native_os.select_native_os_tools(os.environ['EMBER_HOST_PREFIX'], str(source),
                                                  compiler_family=host_family)
        extra = host_prefix.load_host_prefix(os.environ['EMBER_HOST_PREFIX'],
            required_roles={'ruby', 'shasum'}, compiler_family=host_family)
        ports = os.environ['EMBER_PORTS_SOURCE']
        git = os.environ.get('EMBER_GIT') or shutil.which('git')
        if not git or not os.path.isabs(git):
            raise SDKError('select a Git executable for source inspection with EMBER_GIT')
        libedit.validate_source(os.environ['EMBER_LIBEDIT_SOURCE'], ports=ports, git=git)
        env = native_os.native_os_environment(native, output / 'routing')
        for package, entry, operation in (
            ('EMBER_LIBARCHIVE_SOURCE', 'scripts/libarchive-source.py', 'libarchive'),
            ('EMBER_LIBEVENT_SOURCE', 'scripts/source-inputs/libevent/source.py', 'libevent'),
        ):
            script = str(Path(ports) / entry)
            if operation == 'libarchive':
                command = [native.tool('python'), '-B', script, 'verify', os.environ[package],
                           '--sdk-source', str(sdk), '--git', git]
            else:
                command = [native.tool('python'), '-B', script, '--sdk-source', str(sdk),
                           '--git', git, 'verify-source', '--source', os.environ[package]]
            process.capture(command, env=env)
        verifier = str(Path(ports) / 'pkgsrc/lang/compiler-rt/files/runtime.py')
        source_record = jsonio.loads_json(process.capture(
            [native.tool('python'), '-B', verifier, '--sdk-source', str(sdk), 'verify-source',
             '--source', os.environ['EMBER_LLVM_SOURCE'], '--git', git], env=env).stdout)
        if source_record.get('format') != 'emberbsd-llvm-source' or source_record.get('version') != 1:
            raise SDKError('invalid prepared LLVM source record')
        llvm = []
        if family == 'clang':
            record = application_toolchain.load_clang_runtime(os.environ['EMBER_SDK_SYSROOT'], target='aarch64')
            selected = llvm_runtime.runtime_paths(os.environ['EMBER_SDK_SYSROOT'], record)
            identity = target_toolchain.clang_compiler(str(compiler), 'aarch64--netbsd',
                                                        host=dict(native.host), env=env)
            application_toolchain.clang_application_link_flags(identity, os.environ['EMBER_SDK_SYSROOT'], record)
            if (source_record.get('llvm_version') != identity['version'] or
                    record['source'] != {key: source_record['source'][key] for key in ('archive', 'sha512')}):
                raise SDKError('prepared LLVM source differs from the selected compiler or runtime')
            llvm = [selected['runtime-prefix'], selected['llvm-resource'], selected['llvm-unwind'],
                    identity['resource_dir'], identity['version'], identity['reported_triplet'],
                    str(Path(os.environ['EMBER_SDK_SYSROOT']) / record['artifacts']['builtins'])]
        else:
            llvm = ['-'] * 7
        libedit = os.environ['EMBER_LIBEDIT_SOURCE']
        llvm_source = os.environ.get('EMBER_LLVM_SOURCE', '-')
        host_bfd = os.environ['EMBER_HOST_BFD_CONFIG']
        jobs = os.environ.get('BUILD_JOBS', '2')
        output.mkdir()
        (output / 'logs').mkdir()
        script = str(Path(__file__).with_name('cix-p1-build-userland.sh').resolve())
        arguments = [script, '--selected', str(source), str(output), str(tools), str(compiler),
                     str(sdk), native.bundle_root, host_family, family, libedit,
                     llvm_source, host_bfd, jobs, git, extra.tool('ruby'), extra.tool('shasum'),
                     os.environ['EMBER_LIBARCHIVE_SOURCE'], os.environ['EMBER_LIBEVENT_SOURCE'], *llvm]
        native_os.run_native_os(native, source=str(source), routing=str(output / 'routing'),
                                cwd=str(source), arguments=arguments, shell=True,
                                log=str(output / 'logs/orchestrator.log'))
    except (SDKError, OSError, ValueError, KeyError, TypeError) as exc:
        raise SystemExit('cix-p1-native: ' + str(exc)) from exc
    print('Reduced static C cohort built. No image or media was written.')


if __name__ == '__main__':
    main()
