# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Shared target-independent component selection for a static console."""


def console(target):
    targets = {
        'aarch64': ('evbarm', 'aarch64', 'aarch64--netbsd', 'ELF64', 'AArch64', ()),
        'riscv32': ('riscv', 'riscv32', 'riscv32--netbsd', 'ELF32', 'RISC-V', ('MKSOFTFLOAT=yes',)),
    }
    if target not in targets:
        raise ValueError('unsupported static console target')
    machine, arch, triplet, elf_class, elf_machine, flags = targets[target]
    return dict(machine=machine, arch=arch, triplet=triplet, elf_class=elf_class,
                elf_machine=elf_machine, flags=flags, headers=('lib/libcurses',),
                libraries=('lib/libterminfo', 'lib/libedit', 'lib/libutil'),
                programs=('bin/sh', 'sbin/init'))


def check_foundation(record, target, runtime):
    """Require the actual OS C stage and its selected bootstrap runtime."""
    selected = console(target)
    try:
        signature = record['signature']
        flags = signature['flags']
        valid = (record['format'] == 'emberbsd-llvm-os-stage' and
                 record['phase'] == 'base' and record['qualification'] == 'built-and-inspected' and
                 signature['target']['arch'] == selected['arch'] and
                 signature['target']['triplet'] == selected['triplet'] and
                 signature['runtime']['manifest'] == runtime and
                 all(flag in flags for flag in ('MKPIC=no', 'MKPIE=no', 'LDSTATIC=-static', *selected['flags'])))
    except (KeyError, TypeError):
        valid = False
    if not valid:
        raise ValueError('C foundation does not match the selected target and bootstrap runtime')


def run_console(arguments):
    """Extend a verified C foundation with the shared native console targets."""
    import os
    import re
    import shutil
    import subprocess
    import sys
    from pathlib import Path

    if len(arguments) != 4:
        raise SystemExit('usage: cix-p1-native.py SOURCE NEW_OUTPUT TOOLDIR TARGET_PREFIX')
    required = ('EMBERBSD_SDK_SOURCE', 'EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE',
                'EMBER_HOST_BFD_CONFIG', 'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE',
                'EMBER_BYACC_SOURCE', 'EMBER_C_FOUNDATION', 'EMBER_LLVM_RUNTIME_STAGE',
                'EMBER_CONSOLE_TARGET')
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise SystemExit('missing required inputs: ' + ', '.join(missing))
    source, output, tools, compiler = (Path(value).absolute() for value in arguments)
    target = os.environ['EMBER_CONSOLE_TARGET']
    settings = console(target)
    sdk = Path(os.environ['EMBERBSD_SDK_SOURCE']).resolve(strict=True)
    sys.path.insert(0, str(sdk / 'src'))
    from emberbsd_sdk import elf, inventory, jsonio, libedit, llvm_runtime, native_os, paths, process, target_toolchain
    from emberbsd_sdk.errors import SDKError
    try:
        source = Path(paths.canonical_existing(source))
        foundation = Path(paths.canonical_existing(os.environ['EMBER_C_FOUNDATION']))
        stage = Path(paths.canonical_existing(os.environ['EMBER_LLVM_RUNTIME_STAGE']))
        tools = Path(paths.canonical_existing(tools))
        compiler = Path(paths.canonical_existing(compiler))
        output = Path(paths.canonical_new(output))
        for item in (source, foundation, stage, tools, compiler, output, sdk):
            if not re.fullmatch(r'/[A-Za-z0-9_./-]+', str(item)):
                raise SDKError('paths must be absolute and contain plain path characters')
        for root in (source, foundation, stage, tools, compiler, sdk):
            paths.reject_overlap(str(output), str(root))
        if output.exists() or output.is_symlink():
            raise SDKError('output must not already exist')
        host = os.environ['EMBER_HOST_PREFIX']
        native = native_os.select_native_os_tools(host, str(source),
            compiler_family=os.environ.get('EMBER_HOST_COMPILER_FAMILY', 'clang'))
        git = os.environ.get('EMBER_GIT') or shutil.which('git')
        if not git or not os.path.isabs(git):
            raise SDKError('select an absolute Git executable')
        def git_output(*args):
            return subprocess.check_output([git, '-C', str(source), *args], text=True).strip()
        if git_output('status', '--porcelain=v1', '--untracked-files=all'):
            raise SDKError('OS source must be clean')
        revision = git_output('rev-parse', 'HEAD')
        timestamp = git_output('show', '-s', '--format=%ct', revision)
        receipt = jsonio.read_json(foundation / '.prebuild-sdk-build.json')
        original = receipt['sources']['os']['commit']
        if subprocess.run([git, '-C', str(source), 'diff', '--quiet', original, revision,
                           '--', '.', ':(exclude)ember/image'], check=False).returncode:
            raise SDKError('OS source outside image producer differs from C foundation')
        if inventory.snapshot_tree(foundation, exclude=('.prebuild-sdk-build.json',
                '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log')) != receipt['outputs']:
            raise SDKError('C foundation contents changed')
        record = llvm_runtime.validate_stage(stage, target=target,
            required_components=('builtins', 'libunwind-static'), base=foundation)
        check_foundation(receipt, target, record)
        if Path(receipt['paths']['tools']).resolve() != tools:
            raise SDKError('OS host tools differ from C foundation')
        env = native_os.native_os_environment(native, output / 'routing')
        identity = target_toolchain.clang_compiler(str(compiler), settings['triplet'],
                                                    host=dict(native.host), env=env)
        if identity != receipt['signature']['compiler']:
            raise SDKError('target compiler differs from C foundation')
        if libedit.validate_source(os.environ['EMBER_LIBEDIT_SOURCE'],
                                   ports=os.environ['EMBER_PORTS_SOURCE'], git=git) != receipt['signature']['libedit']:
            raise SDKError('libedit differs from C foundation')
        verifier = str(Path(os.environ['EMBER_PORTS_SOURCE']) / 'pkgsrc/lang/compiler-rt/files/runtime.py')
        command = [native.tool('python'), '-B', verifier, '--sdk-source', str(sdk), 'verify',
                   '--stage', str(stage), '--target', target, '--phase', 'bootstrap', '--base', str(foundation),
                   '--source', os.environ['EMBER_LLVM_SOURCE'], '--git', git]
        if os.environ.get('EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE'):
            command += ['--original-source', os.environ['EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE']]
        process.capture(command, env=env)
        selected = llvm_runtime.runtime_paths(stage, record)
        jobs = os.environ.get('BUILD_JOBS', '2')
        if not jobs.isdecimal() or not 1 <= int(jobs) <= 32:
            raise SDKError('BUILD_JOBS must be between 1 and 32')
        output.mkdir()
        (output / 'logs').mkdir()
        shutil.copytree(foundation, output / 'dest', symlinks=True,
            ignore=shutil.ignore_patterns('.prebuild-sdk-build.json', '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log'))
        shell = str(Path(__file__).with_name('cix-p1-build-userland.sh'))
        command = [shell, '--console-selected', str(source), str(output), str(tools), str(compiler), str(sdk),
                   native.bundle_root, os.environ.get('EMBER_HOST_COMPILER_FAMILY', 'clang'),
                   settings['machine'], settings['arch'], os.environ['EMBER_LIBEDIT_SOURCE'],
                   os.environ['EMBER_LLVM_SOURCE'], os.environ['EMBER_HOST_BFD_CONFIG'], os.environ['EMBER_BYACC_SOURCE'],
                   selected['runtime-prefix'], selected['llvm-resource'], selected['llvm-unwind'],
                   identity['resource_dir'], jobs, timestamp, revision,
                   ' '.join(settings['headers']), ' '.join(settings['libraries']), ' '.join(settings['programs'])]
        native_os.run_native_os(native, source=str(source), routing=str(output / 'routing'), cwd=str(source),
                                arguments=command, shell=True, log=str(output / 'logs/orchestrator.log'))
        readelf = str(compiler / 'bin/llvm-readelf')
        for name in settings['programs']:
            binary = output / 'dest' / name
            header = elf.read_elf_header(binary, receipt['signature']['target']['elf'], 2,
                                         riscv32_soft_float=target == 'riscv32')
            if header is None or header['class'] != (32 if target == 'riscv32' else 64):
                raise SDKError('invalid console executable: ' + name)
            segments = process.capture([readelf, '-l', str(binary)], env=env).stdout.decode()
            if re.search(r'^\s*(?:INTERP|DYNAMIC)\s', segments, re.M):
                raise SDKError('console executable is not static: ' + name)
        if git_output('rev-parse', 'HEAD') != revision or git_output('status', '--porcelain=v1', '--untracked-files=all'):
            raise SDKError('OS source changed during build')
        if inventory.snapshot_tree(foundation, exclude=('.prebuild-sdk-build.json',
                '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log')) != receipt['outputs']:
            raise SDKError('C foundation changed during build')
        (output / 'BUILD-INPUTS.txt').write_text('source=' + revision + '\ntarget=' + target +
            '\nprofile=static-console\nfoundation=' + original + '\n')
        print('Built static console and 4 MiB FFS image; target execution not checked.')
    except (SDKError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('static-console: ' + str(exc)) from exc
