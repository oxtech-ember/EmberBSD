# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Shared target-independent component selection for a static console."""

from pathlib import Path


def stage_overlay(overlay, root):
    """Add regular files without replacing base paths or following symlinks."""
    import os
    import shutil
    import stat

    overlay, root = Path(overlay), Path(root)
    if overlay.is_symlink() or root.is_symlink() or not overlay.is_dir() or not root.is_dir():
        raise ValueError('overlay and root must be existing directories')
    entries = []
    for directory, dirs, files in os.walk(overlay, followlinks=False):
        for name in sorted(dirs + files):
            source = Path(directory) / name
            dest = root / source.relative_to(overlay)
            if source.is_symlink() or not (source.is_dir() or source.is_file()):
                raise ValueError('overlay must contain only regular files and directories')
            if dest.is_symlink() or (dest.exists() and not (source.is_dir() and dest.is_dir())):
                raise ValueError('overlay conflicts with a base path')
            entries.append((source, dest, dest.exists()))
    for source, dest, existed in entries:
        if source.is_dir():
            dest.mkdir(exist_ok=True)
        else:
            shutil.copy2(source, dest)
    for source, dest, existed in reversed(entries):
        if source.is_dir() and not existed:
            dest.chmod(stat.S_IMODE(source.stat().st_mode))


def console(target, compact=False):
    targets = {
        'aarch64': ('evbarm', 'aarch64', 'aarch64--netbsd', 'ELF64', 'AArch64', ()),
        'riscv32': ('riscv', 'riscv32', 'riscv32--netbsd', 'ELF32', 'RISC-V', ('MKSOFTFLOAT=yes',)),
    }
    if target not in targets:
        raise ValueError('unsupported static console target')
    machine, arch, triplet, elf_class, elf_machine, flags = targets[target]
    libraries = ('lib/libterminfo', 'lib/libedit', 'lib/libutil')
    programs = ('bin/sh', 'sbin/init')
    if compact:
        libraries += ('lib/libm', 'lib/libprop', 'lib/libkvm')
        programs += ('bin/hostname', 'sbin/sysctl', 'sbin/dmesg',
                     'sbin/ifconfig', 'bin/cat', 'bin/ls', 'bin/chmod',
                     'bin/cp', 'bin/mkdir', 'bin/sleep', 'sbin/reboot')
    return dict(machine=machine, arch=arch, triplet=triplet, elf_class=elf_class,
                elf_machine=elf_machine, flags=flags, headers=('lib/libcurses',),
                libraries=libraries, programs=programs, compact=compact)


def check_foundation(record, target, runtime):
    """Require the actual OS C stage and its selected bootstrap runtime."""
    selected = console(target)
    try:
        signature = record['signature']
        flags = signature['flags']
        valid = (record['format'] == 'emberbsd-llvm-os-stage' and
                 record['phase'] == 'base' and
                 signature['target']['arch'] == selected['arch'] and
                 signature['target']['triplet'] == selected['triplet'] and
                 signature['runtime']['manifest'] == runtime and
                 all([flag for flag in flags if flag.split('=', 1)[0] == required.split('=', 1)[0]] == [required]
                     for required in ('MKPIC=no', 'MKPICINSTALL=no', 'MKCOMPAT=no', 'MKCXX=no',
                                      'MKPIE=no', 'LDSTATIC=-static', *selected['flags'])))
    except (KeyError, TypeError):
        valid = False
    if not valid:
        raise ValueError('C foundation does not match the selected target and bootstrap runtime')


def check_compiler_family(family):
    if family != 'clang':
        raise ValueError('static console supports only the LLVM C foundation (clang)')


def check_os_tools(tools, native, search_path, validate, compact=False):
    names = ('nbmkdep', 'nbmtree', 'nbmakefs')
    if compact:
        names += ('nbcrunchgen',)
    for name in names:
        validate(str(tools), str(tools / 'bin' / name), dict(native.host),
                 search_path=search_path)
    if compact:
        for name in ('llvm-nm', 'llvm-objcopy'):
            validate(native.bundle_root, str(Path(native.bundle_root) / 'bin' / name),
                     dict(native.host), search_path=search_path)


def check_compiler_identity(actual, recorded):
    keys = ('family', 'version', 'triplet')
    try:
        matches = all(actual[key] == recorded[key] for key in keys)
    except (KeyError, TypeError):
        matches = False
    if not matches:
        raise ValueError('target compiler differs from C foundation')


def source_worktree_state(git, source):
    """Record the tracked diff and untracked bytes without requiring a clean tree."""
    import hashlib
    import os
    import stat
    import subprocess
    from pathlib import Path

    source = Path(source)
    def capture(*args):
        return subprocess.check_output([git, '-C', str(source), *args])
    status = capture('status', '--porcelain=v1', '-z', '--untracked-files=all')
    digest = hashlib.sha256()
    def add(data):
        digest.update(len(data).to_bytes(8, 'big'))
        digest.update(data)
    add(status)
    add(capture('diff', '--cached', '--binary', '--no-ext-diff', '--no-textconv'))
    add(capture('diff', '--binary', '--no-ext-diff', '--no-textconv'))
    names = capture('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')
    for name in sorted(n for n in names if n):
        path = source / os.fsdecode(name)
        mode = path.lstat().st_mode
        add(name)
        add(str(stat.S_IMODE(mode)).encode('ascii'))
        if stat.S_ISLNK(mode):
            add(b'link')
            add(os.fsencode(os.readlink(path)))
        elif stat.S_ISREG(mode):
            add(b'file')
            content = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    content.update(block)
            add(content.digest())
        else:
            raise ValueError('untracked source input is not a regular file or symlink')
    return {'state': 'modified' if status else 'clean', 'sha256': digest.hexdigest()}


def run_console(arguments):
    """Extend a verified C foundation with the shared native console targets."""
    import hashlib
    import json
    import os
    import re
    import shutil
    import subprocess
    import sys
    from pathlib import Path

    if len(arguments) != 4:
        raise SystemExit('usage: cix-p1-native.py SOURCE NEW_OUTPUT TOOLDIR TARGET_PREFIX')
    try:
        check_compiler_family(os.environ.get('EMBER_TARGET_COMPILER_FAMILY', 'clang'))
    except ValueError as exc:
        raise SystemExit('static-console: ' + str(exc)) from exc
    required = ('EMBERBSD_SDK_SOURCE', 'EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE',
                'EMBER_HOST_BFD_CONFIG', 'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE',
                'EMBER_BYACC_SOURCE', 'EMBER_C_FOUNDATION', 'EMBER_LLVM_RUNTIME_STAGE',
                'EMBER_CONSOLE_TARGET')
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise SystemExit('missing required inputs: ' + ', '.join(missing))
    source, output, tools, compiler = (Path(value).absolute() for value in arguments)
    target = os.environ['EMBER_CONSOLE_TARGET']
    settings = console(target, os.environ.get('EMBER_USERLAND_PROFILE') == 'console-tools')
    sdk = Path(os.environ['EMBERBSD_SDK_SOURCE']).resolve(strict=True)
    sys.path.insert(0, str(sdk / 'src'))
    from emberbsd_sdk import cpu_capacity, elf, host_prefix, inventory, jsonio, libedit, llvm_runtime, native_os, paths, process, sourceprep, target_toolchain
    from emberbsd_sdk.errors import SDKError
    try:
        source = Path(paths.canonical_existing(source))
        foundation = Path(paths.canonical_existing(os.environ['EMBER_C_FOUNDATION']))
        stage = Path(paths.canonical_existing(os.environ['EMBER_LLVM_RUNTIME_STAGE']))
        tools = Path(paths.canonical_existing(tools))
        compiler = Path(paths.canonical_existing(compiler))
        output = Path(paths.canonical_new(output))
        extra_paths = [Path(paths.canonical_existing(os.environ[name])) for name in
                       ('EMBER_HOST_PREFIX', 'EMBER_LIBEDIT_SOURCE', 'EMBER_HOST_BFD_CONFIG',
                        'EMBER_LLVM_SOURCE', 'EMBER_PORTS_SOURCE', 'EMBER_BYACC_SOURCE')]
        for item in (source, foundation, stage, tools, compiler, output, sdk, *extra_paths):
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
        revision = git_output('rev-parse', 'HEAD')
        worktree = source_worktree_state(git, source)
        timestamp = git_output('show', '-s', '--format=%ct', revision)
        receipt = jsonio.read_json(foundation / '.prebuild-sdk-build.json')
        original = receipt['sources']['os']['commit']
        if inventory.snapshot_tree(foundation, exclude=('.prebuild-sdk-build.json',
                '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log')) != receipt['outputs']:
            raise SDKError('C foundation contents changed')
        record = llvm_runtime.validate_stage(stage, target=target,
            required_components=('builtins', 'libunwind-static'), base=foundation)
        check_foundation(receipt, target, record)
        env = native_os.native_os_environment(native, output / 'routing')
        check_os_tools(tools, native, env['PATH'], host_prefix.validate_host_executable,
                       compact=settings['compact'])
        identity = target_toolchain.clang_compiler(str(compiler), settings['triplet'],
                                                    host=dict(native.host), env=env)
        check_compiler_identity(identity, receipt['signature']['compiler'])
        overlay = os.environ.get('EMBER_CONSOLE_OVERLAY', '') if settings['compact'] else ''
        extra = os.environ.get('EMBER_CONSOLE_PROGRAM', '') if settings['compact'] else ''
        overlay_state = None
        extra_digest = None
        if overlay:
            overlay = paths.canonical_existing(overlay)
            paths.reject_overlap(str(output), overlay)
            if not Path(overlay).is_dir() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', overlay):
                raise SDKError('console overlay must be an absolute plain-path directory')
            overlay_state = inventory.snapshot_tree(overlay)
        if extra:
            extra = paths.canonical_existing(extra)
            paths.reject_overlap(str(output), extra)
            if not Path(extra).is_file() or not re.fullmatch(r'/[A-Za-z0-9_./-]+', extra):
                raise SDKError('console program must be an absolute plain-path file')
            if elf.read_elf_header(extra, receipt['signature']['target']['elf'], 2,
                                   riscv32_soft_float=target == 'riscv32') is None:
                raise SDKError('console program has incompatible target ABI')
            segments = process.capture([str(compiler / 'bin/llvm-readelf'), '-l', extra], env=env).stdout.decode()
            if re.search(r'^\s*(?:INTERP|DYNAMIC)\s', segments, re.M):
                raise SDKError('console program must be statically linked')
            extra_digest = hashlib.sha256(Path(extra).read_bytes()).hexdigest()
        libedit.validate_source(os.environ['EMBER_LIBEDIT_SOURCE'],
                                ports=os.environ['EMBER_PORTS_SOURCE'], git=git)
        sourceprep.read_prepared_tree_receipt(os.environ['EMBER_BYACC_SOURCE'], package='devel/byacc')
        current = str(Path(os.environ['EMBER_PORTS_SOURCE']) / 'scripts/prepared-source-current.py')
        process.capture([native.tool('python'), '-B', current, '--sdk-source', str(sdk), '--git', git,
                         '--source', 'byacc=' + os.environ['EMBER_BYACC_SOURCE']], env=env)
        verifier = str(Path(os.environ['EMBER_PORTS_SOURCE']) / 'pkgsrc/lang/compiler-rt/files/runtime.py')
        source_record = jsonio.loads_json(process.capture([native.tool('python'), '-B', verifier,
            '--sdk-source', str(sdk), 'verify-source', '--source', os.environ['EMBER_LLVM_SOURCE'],
            '--git', git], env=env).stdout)
        if (source_record['llvm_version'] != identity['version'] or
                record['source'] != {key: source_record['source'][key] for key in ('archive', 'sha512')}):
            raise SDKError('LLVM source differs from the selected runtime')
        command = [native.tool('python'), '-B', verifier, '--sdk-source', str(sdk), 'verify',
                   '--stage', str(stage), '--target', target, '--phase', 'bootstrap', '--base', str(foundation),
                   '--source', os.environ['EMBER_LLVM_SOURCE'], '--git', git]
        if os.environ.get('EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE'):
            command += ['--original-source', os.environ['EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE']]
        process.capture(command, env=env)
        selected = llvm_runtime.runtime_paths(stage, record)
        jobs = str(cpu_capacity.resolve_jobs(os.environ.get('BUILD_JOBS'), option='BUILD_JOBS'))
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
                   ' '.join(settings['headers']), ' '.join(settings['libraries']), ' '.join(settings['programs']),
                   native.tool('python'), 'console-tools' if settings['compact'] else 'console', overlay, extra,
                   native.tool('awk')]
        native_os.run_native_os(native, source=str(source), routing=str(output / 'routing'), cwd=str(source),
                                arguments=command, shell=True, log=str(output / 'logs/orchestrator.log'))
        readelf = str(compiler / 'bin/llvm-readelf')
        for name in (('bin/console-tools',) if settings['compact'] else settings['programs']):
            binary = output / ('root' if settings['compact'] else 'dest') / name
            header = elf.read_elf_header(binary, receipt['signature']['target']['elf'], 2,
                                         riscv32_soft_float=target == 'riscv32')
            if header is None or header['class'] != (32 if target == 'riscv32' else 64):
                raise SDKError('invalid console executable: ' + name)
            segments = process.capture([readelf, '-l', str(binary)], env=env).stdout.decode()
            if re.search(r'^\s*(?:INTERP|DYNAMIC)\s', segments, re.M):
                raise SDKError('console executable is not static: ' + name)
        if git_output('rev-parse', 'HEAD') != revision or source_worktree_state(git, source) != worktree:
            raise SDKError('OS source changed during build')
        if overlay and inventory.snapshot_tree(overlay) != overlay_state:
            raise SDKError('console overlay changed during build')
        if extra and hashlib.sha256(Path(extra).read_bytes()).hexdigest() != extra_digest:
            raise SDKError('console program changed during build')
        if inventory.snapshot_tree(foundation, exclude=('.prebuild-sdk-build.json',
                '.prebuild-sdk-build-incomplete', '.prebuild-sdk-build.log')) != receipt['outputs']:
            raise SDKError('C foundation changed during build')
        image = output / 'root.fs'
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        (output / 'ROOT-SHA256.txt').write_text(digest + '  root.fs\n')
        (output / 'BUILD-INPUTS.txt').write_text('source=' + revision + '\ntarget=' + target +
            '\nsource-state=' + worktree['state'] + '\nsource-worktree-sha256=' + worktree['sha256'] +
            '\nprofile=static-' + ('console-tools' if settings['compact'] else 'console') +
            '-bring-up\nfoundation=' + original + '\n' +
            ('overlay-sha256=' + hashlib.sha256(json.dumps(overlay_state, sort_keys=True).encode()).hexdigest() + '\n'
             if overlay_state is not None else '') +
            ('program-sha256=' + extra_digest + '\n' if extra_digest else ''))
        print('Built static bring-up console and 4 MiB FFS image; target execution not checked.')
    except (SDKError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('static-console: ' + str(exc)) from exc


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 4 or sys.argv[1] != 'stage-overlay':
        raise SystemExit('usage: static-profile.py stage-overlay DIRECTORY ROOT')
    stage_overlay(sys.argv[2], sys.argv[3])
