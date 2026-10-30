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

TOOLS = Path(__file__).resolve().parent
TARGETS = {'aarch64': 'BSD-aarch64', 'x86_64': 'BSD-x86_64',
           'i386': 'BSD-x86-elf', 'arm': 'BSD-armv4', 'sparc': 'BSD-sparcv8',
           'sparc64': 'BSD-sparc64', 'powerpc': 'BSD-ppc', 'powerpc64': 'BSD-ppc64',
           'riscv32': 'BSD-riscv32', 'riscv64': 'BSD-riscv64', 'ia64': 'BSD-ia64',
           'hppa': 'BSD-generic32', 'sh3': 'BSD-generic32'}


def compiler_command(cc, family, triplet, sysroot):
    return cc + (' --target=' + triplet if family == 'clang' else '') + ' --sysroot=' + sysroot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'output', 'arch', 'cc', 'sysroot', 'perl', 'gmake', 'ar', 'ranlib', 'tmpdir', 'path', 'shell', 'object-root', 'family', 'triplet', 'cpu'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    if args.cpu not in TARGETS or args.family not in ('clang', 'gcc') or not 1 <= args.jobs <= 256:
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
    target = 'BSD-ppc64le' if args.arch == 'powerpc64le' else TARGETS[args.cpu]
    triplet = args.triplet
    if not triplet or any(ch not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.+-' for ch in triplet):
        parser.error('invalid target triplet')
    lock_path = object_root / '.openssl-generation.lock'
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    if not stat.S_ISREG(os.fstat(lock_fd).st_mode):
        parser.error('generation lock is not a regular file')
    fcntl.flock(lock_fd, fcntl.LOCK_EX)
    # The descriptor remains open until all output is complete.
    # This is a rebuild key, not a qualification or a requirement for a receipt.
    # The SDK checks prepared-source integrity before invoking the OS build.
    digest = hashlib.sha256()
    for path in sorted(source.rglob('*')):
        if path.is_file() and (path.name in ('Configure', 'build.info', 'VERSION.dat') or
                               path.suffix in ('.in', '.pm', '.pl', '.conf')):
            digest.update(str(path.relative_to(source)).encode() + b'\0')
            digest.update(path.read_bytes())
    # Configure can probe BSD KTLS headers if upstream enables that option.
    # If KTLS exists, include its transitive header environment in the key.
    include_root = Path(args.sysroot) / 'usr/include'
    ktls = include_root / 'sys/ktls.h'
    header_paths = sorted(include_root.rglob('*')) if ktls.is_file() else [include_root / 'sys/types.h', ktls]
    for path in header_paths:
        if path.is_file() and (include_root / 'openssl') not in path.parents:
            digest.update(str(path.relative_to(include_root)).encode() + b'\0')
            digest.update(path.read_bytes())
        elif path in (ktls, include_root / 'sys/types.h'):
            digest.update(str(path.relative_to(include_root)).encode() + b'\0missing')
    for name in ('cc', 'perl', 'gmake', 'ar', 'ranlib', 'shell'):
        with open(getattr(args, name), 'rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
    for path in (TOOLS / 'prepare-generated.py', TOOLS / 'generated-sources.pl', TOOLS / 'config-sources.pl'):
        digest.update(path.read_bytes())
    key = {'source': str(source), 'inputs': digest.hexdigest(), 'arch': args.arch, 'cpu': args.cpu,
           'cc': args.cc, 'family': args.family, 'triplet': triplet, 'sysroot': args.sysroot, 'perl': args.perl, 'gmake': args.gmake,
           'ar': args.ar, 'ranlib': args.ranlib, 'shell': args.shell, 'path': args.path}
    marker = output / '.complete'
    owned = {'format': 'emberbsd-openssl-generated', 'object_root': str(object_root)}
    def payload_hash():
        result = hashlib.sha256()
        for path in sorted(output.rglob('*')):
            if path.name in ('.owned', '.complete'):
                continue
            relative = str(path.relative_to(output)).encode()
            if path.is_symlink():
                result.update(b'L' + relative + b'\0' + os.readlink(path).encode())
            elif path.is_file():
                result.update(b'F' + relative + b'\0')
                with path.open('rb') as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                        result.update(chunk)
        return result.hexdigest()
    required = ('crypto.mk', 'crypto-shared.mk', 'default.mk', 'common.mk', 'ssl.mk',
                'ssl-shared.mk', 'apps.mk', 'legacy.mk', 'legacy-module.mk', 'test.mk', 'cli.mk', 'libcrypto.ld', 'libssl.ld', 'providers/legacy.ld', 'include/openssl/configuration.h')
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
            if (isinstance(previous, dict) and previous.get('key') == key and
                    all((output / item).is_file() for item in required) and
                    previous.get('payload') == payload_hash()):
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
    compiler = compiler_command(args.cc, args.family, triplet, args.sysroot)
    env = {'PATH': args.path,
           'HOME': str(output), 'TMPDIR': args.tmpdir,
           'CC': compiler,
           'AR': args.ar, 'RANLIB': args.ranlib,
           'LDFLAGS': ('--target=' + triplet + ' ' if args.family == 'clang' else '') + '--sysroot=' + args.sysroot}
    def run(cmd, **kw):
        return subprocess.run(cmd, cwd=output, env=env, check=True, **kw)
    run([args.perl, str(source / 'Configure'), target, '--prefix=/usr', '--openssldir=/etc/openssl'])
    run([args.gmake, 'SHELL=' + args.shell, '-j' + str(args.jobs), 'build_generated'])
    listed = run([args.perl, '-I.', str(TOOLS / 'generated-sources.pl')], capture_output=True, text=True).stdout.splitlines()
    if listed:
        run([args.gmake, 'SHELL=' + args.shell, '-j' + str(args.jobs), *listed])
    run([args.gmake, 'SHELL=' + args.shell, 'libcrypto.ld', 'libssl.ld', 'providers/legacy.ld'])
    for group in ('crypto', 'crypto-shared', 'default', 'common', 'ssl', 'ssl-shared', 'apps', 'legacy', 'legacy-module', 'test', 'cli'):
        with (output / (group + '.mk')).open('w') as stream:
            run([args.perl, '-I.', str(TOOLS / 'config-sources.pl'), group], stdout=stream)
    for relative, (contents, atime, mtime) in preserved.items():
        path = output / relative
        if path.is_file() and not path.is_symlink() and hashlib.sha256(path.read_bytes()).digest() == contents:
            os.utime(path, ns=(atime, mtime))
    temporary = output / '.complete.tmp'
    temporary.write_text(json.dumps({'key': key, 'payload': payload_hash()}, sort_keys=True) + '\n')
    os.replace(temporary, marker)
    os.close(lock_fd)


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as exc:
        print('OpenSSL generation:', exc, file=sys.stderr)
        sys.exit(1)
