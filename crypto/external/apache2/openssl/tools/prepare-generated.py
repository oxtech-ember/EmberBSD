#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Prepare native OS OpenSSL generation in a dedicated target object directory."""
import argparse
import hashlib
import fcntl
import stat
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import shlex

TOOLS = Path(__file__).resolve().parent
TARGETS = {'aarch64': 'BSD-aarch64', 'x86_64': 'BSD-x86_64',
           'i386': 'BSD-x86-elf', 'arm': 'BSD-armv4', 'sparc': 'BSD-sparcv8',
           'sparc64': 'BSD-sparc64', 'powerpc': 'BSD-ppc', 'powerpc64': 'BSD-ppc64',
           'riscv32': 'BSD-riscv32', 'riscv64': 'BSD-riscv64', 'ia64': 'BSD-ia64',
           'hppa': 'BSD-generic32', 'sh3': 'BSD-generic32'}


def configured_target(arch, cpu):
    by_arch = {'riscv32': 'BSD-riscv32', 'riscv64': 'BSD-riscv64',
               'powerpc64': 'BSD-ppc64', 'powerpc64le': 'BSD-ppc64le'}
    return by_arch.get(arch) or TARGETS.get(cpu)


def compiler_command(cc, family, triplet, sysroot, flags=()):
    args = [cc, *flags]
    if family == 'clang':
        args.append('--target=' + triplet)
    args.append('--sysroot=' + sysroot)
    return shlex.join(args)


def input_identity(source, object_root):
    """Use the identities already selected by the SDK, without reading payloads."""
    receipt = source / 'emberbsd-prepared-source.json'
    if not receipt.is_file():
        return None
    prepared = json.loads(receipt.read_text())
    if (not isinstance(prepared, dict) or prepared.get('format') != 'emberbsd-prepared-source' or
            prepared.get('version') != 1 or prepared.get('package') != 'security/openssl'):
        raise ValueError('invalid prepared OpenSSL source identity')
    for parent in (object_root, *object_root.parents):
        context = parent / '.prebuild-sdk-context.json'
        if not context.is_file():
            continue
        record = json.loads(context.read_text())
        signature = record.get('signature') if isinstance(record, dict) else None
        if (not isinstance(record, dict) or record.get('format') != 'emberbsd-sysroot-object-context' or record.get('version') != 1 or
                not isinstance(signature, dict) or signature.get('objects') != str(parent) or
                not all(name in signature for name in ('compiler', 'native_toolset', 'runtime',
                                                       'target', 'flags', 'common_sources')) or
                not isinstance(record.get('sources'), dict)):
            raise ValueError('invalid SDK object input context: ' + str(context))
        return dict(source=prepared, context=record)
    # Direct make users can still generate files without an SDK context, but
    # paths alone do not identify their compiler and sysroot for reuse.
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'output', 'arch', 'cc', 'sysroot', 'perl', 'gmake', 'ar', 'ranlib', 'tmpdir', 'path', 'shell', 'object-root', 'family', 'triplet', 'cpu'):
        parser.add_argument('--' + name, required=True)
    for name in ('cc-command', 'cc-flags', 'cc-options'):
        parser.add_argument('--' + name, default='')
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    try:
        cc_words = shlex.split(args.cc_command) if args.cc_command else [args.cc]
        if not cc_words or cc_words[0] != args.cc:
            parser.error('selected CC does not match compiler executable')
        cc_args = cc_words[1:] + shlex.split(args.cc_flags) + shlex.split(args.cc_options)
    except ValueError as exc:
        parser.error('invalid compiler flags: ' + str(exc))
    if configured_target(args.arch, args.cpu) is None or args.family not in ('clang', 'gcc') or not 1 <= args.jobs <= 256:
        parser.error('unsupported architecture, compiler family or invalid job count')
    for name in ('source', 'output', 'cc', 'sysroot', 'perl', 'gmake', 'ar', 'ranlib', 'tmpdir', 'shell', 'object_root'):
        value = getattr(args, name)
        if not os.path.isabs(value) or any(c.isspace() for c in value):
            parser.error(name + ' must be an absolute path without whitespace')
    source, output = Path(args.source), Path(args.output)
    if not (source / 'Configure').is_file() or not Path(args.sysroot).is_dir():
        parser.error('missing prepared source or target sysroot directory')
    if not output.parent.is_dir():
        parser.error('output parent must exist')
    if output.is_symlink():
        parser.error('output cannot be a symbolic link')
    source = source.resolve(strict=True)
    output = output.parent.resolve(strict=True) / output.name
    object_root = Path(args.object_root).resolve(strict=True)
    if output != object_root / 'generated':
        parser.error('output must be the generated directory of the OS object root')
    if output == source or source in output.parents or output in source.parents:
        parser.error('output must not overlap the prepared source')
    if not Path(args.tmpdir).is_dir():
        parser.error('temporary directory does not exist')
    for name in ('cc', 'perl', 'gmake', 'ar', 'ranlib', 'shell'):
        if not os.access(getattr(args, name), os.X_OK):
            parser.error(name + ' is not executable')
    target = configured_target(args.arch, args.cpu)
    triplet = args.triplet
    if not triplet or any(ch not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.+-' for ch in triplet):
        parser.error('invalid target triplet')
    lock_path = object_root / '.openssl-generation.lock'
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    if not stat.S_ISREG(os.fstat(lock_fd).st_mode):
        parser.error('generation lock is not a regular file')
    fcntl.flock(lock_fd, fcntl.LOCK_EX)
    # The descriptor remains open until all output is complete.
    # Source revisions, generator code, toolchain, native tools and sysroot
    # configuration are already identified by the SDK object context.
    identity = input_identity(source, object_root)
    inputs = (hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
              if identity is not None else None)
    key = {'source': str(source), 'inputs': inputs, 'arch': args.arch, 'cpu': args.cpu,
           'cc': args.cc, 'cc_args': cc_args, 'family': args.family, 'triplet': triplet, 'sysroot': args.sysroot, 'perl': args.perl, 'gmake': args.gmake,
           'ar': args.ar, 'ranlib': args.ranlib, 'shell': args.shell, 'path': args.path}
    marker = output / '.complete'
    owned = {'format': 'emberbsd-openssl-generated', 'object_root': str(object_root)}
    preserved = {}
    if output.exists():
        try:
            owner = json.loads((output / '.owned').read_text())
        except (OSError, ValueError):
            owner = None
        if owner != owned:
            parser.error('refusing to replace an unowned output directory')
        if marker.is_file():
            try:
                previous = json.loads(marker.read_text())
            except (OSError, ValueError):
                previous = None
            if identity is not None and isinstance(previous, dict) and previous.get('key') == key:
                os.close(lock_fd)
                return
        # A changed configuration may reproduce identical generated files.
        # Preserve their timestamps so native make does not recompile them.
        for path in output.rglob('*'):
            if path.is_file() and not path.is_symlink() and path.name not in ('.owned', '.complete'):
                contents = hashlib.sha256(path.read_bytes()).digest()
                st = path.stat()
                preserved[str(path.relative_to(output))] = (contents, st.st_atime_ns, st.st_mtime_ns)
        shutil.rmtree(output)
    output.mkdir()
    (output / '.owned').write_text(json.dumps(owned, sort_keys=True) + '\n')
    compiler = compiler_command(args.cc, args.family, triplet, args.sysroot, cc_args)
    env = {'PATH': args.path,
           'TMPDIR': args.tmpdir,
           'CC': compiler,
           'AR': args.ar, 'RANLIB': args.ranlib,
           'LDFLAGS': ('--target=' + triplet + ' ' if args.family == 'clang' else '') + '--sysroot=' + args.sysroot}
    if 'HOME' in os.environ:
        env['HOME'] = os.environ['HOME']
    def run(cmd, **kw):
        return subprocess.run(cmd, cwd=output, env=env, check=True, **kw)
    run([args.perl, str(source / 'Configure'), target, '--prefix=/usr', '--openssldir=/etc/openssl'])
    run([args.gmake, 'SHELL=' + args.shell, '-j' + str(args.jobs), 'build_generated'])
    listed = run([args.perl, '-I.', str(TOOLS / 'generated-sources.pl')], capture_output=True, text=True).stdout.splitlines()
    if listed:
        run([args.gmake, 'SHELL=' + args.shell, '-j' + str(args.jobs), *listed])
    run([args.gmake, 'SHELL=' + args.shell, 'libcrypto.ld', 'libssl.ld', 'providers/legacy.ld'])
    for library in ('crypto', 'ssl'):
        run([sys.executable, '-B', str(TOOLS / 'map-exports.py'), 'lib' + library + '.ld', library + '.expsym'])
    for group in ('crypto', 'crypto-shared', 'default', 'common', 'ssl', 'ssl-shared', 'apps', 'legacy', 'legacy-module', 'test', 'cli'):
        with (output / (group + '.mk')).open('w') as stream:
            run([args.perl, '-I.', str(TOOLS / 'config-sources.pl'), group], stdout=stream)
    for relative, (contents, atime, mtime) in preserved.items():
        path = output / relative
        if path.is_file() and not path.is_symlink() and hashlib.sha256(path.read_bytes()).digest() == contents:
            os.utime(path, ns=(atime, mtime))
    temporary = output / '.complete.tmp'
    temporary.write_text(json.dumps({'key': key}, sort_keys=True) + '\n')
    os.replace(temporary, marker)
    os.close(lock_fd)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print('OpenSSL generation:', exc, file=sys.stderr)
        sys.exit(1)
