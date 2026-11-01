#!/usr/bin/env python3
"""Prepare an object-only OpenSSH configure view and run the target adapter."""
import argparse
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--clean', action='store_true')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--target-lib', type=Path, action='append', default=[])
    parser.add_argument('--object', type=Path, required=True)
    parser.add_argument('--objdir', type=Path)
    parser.add_argument('--clean-helper', type=Path)
    parser.add_argument('--os-source', type=Path)
    parser.add_argument('--autoconf', type=Path)
    parser.add_argument('--make', type=Path)
    parser.add_argument('--makefile-dir', type=Path)
    args = parser.parse_args()
    obj = args.object
    if not obj.is_absolute() or '..' in obj.parts or obj.resolve() != obj:
        parser.error('config object must be an absolute path without symlinks')
    if args.clean:
        if args.objdir is None or args.clean_helper is None or args.os_source is None:
            parser.error('clean requires --objdir, --os-source and --clean-helper')
        root = args.objdir
        if not root.is_absolute() or '..' in root.parts or root.resolve() != root:
            parser.error('objdir must be an absolute path without symlinks')
        if obj.parent != root:
            parser.error('config output must be directly inside its objdir')
        if not root.exists() or not obj.exists():
            return
        os_source = args.os_source.resolve(strict=True)
        if root == os_source or os_source in root.parents:
            parser.error('refusing to clean inside OS source')
        with (root / '.openssh-config.lock').open('a+b') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            subprocess.run([sys.executable, '-B', str(args.clean_helper),
                            '--objdir', str(root), '--output', str(obj)], check=True)
        return
    if any(value is None for value in (args.source, args.os_source, args.autoconf,
                                        args.make, args.makefile_dir)):
        parser.error('generation requires source, OS source, autoconf and make')
    source = args.source.resolve(strict=True)
    os_source = args.os_source.resolve(strict=True)
    autoconf = args.autoconf.resolve(strict=True)
    make = args.make.resolve(strict=True)
    if obj == source or source in obj.parents or obj == os_source or os_source in obj.parents:
        raise ValueError('configure objects must be outside both source trees')
    obj.parent.mkdir(parents=True, exist_ok=True)
    with (obj.parent / '.openssh-config.lock').open('a+b') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        obj.mkdir(exist_ok=True)
        view = obj / 'source-view'
        if view.is_symlink():
            raise ValueError('source-view must be an object directory')
        view.mkdir(exist_ok=True)
        inventory_file = obj / '.source-entries.json'
        previous = json.loads(inventory_file.read_text()) if inventory_file.exists() else []
        if not isinstance(previous, list) or any(
                not isinstance(name, str) or name in ('', '.', '..') or
                Path(name).name != name for name in previous):
            raise ValueError('invalid source-view inventory')
        reserved = {'configure', 'configure.new', 'autom4te.cache'}
        entries = {p.name: p for p in source.iterdir() if p.name not in reserved}
        for name in previous:
            if name not in entries:
                old = view / name
                if not old.is_symlink():
                    raise ValueError('unexpected old source-view entry: ' + str(old))
                old.unlink()
        for name, target in entries.items():
            link = view / name
            if link.is_symlink():
                if os.readlink(link) == str(target):
                    continue
                link.unlink()
            elif link.exists():
                raise ValueError('unexpected source-view entry: ' + str(link))
            link.symlink_to(target, target_is_directory=target.is_dir())
        inventory_file.write_text(json.dumps(sorted(entries)) + '\n')
        digest = hashlib.sha256()

        def add(value):
            digest.update(len(value).to_bytes(8, 'big'))
            digest.update(value)

        add(os.fsencode(source))
        add(os.fsencode(autoconf))
        for item in [source / 'configure.ac', source / 'aclocal.m4', *sorted((source / 'm4').glob('*.m4'))]:
            add(os.fsencode(item.relative_to(source)))
            add(item.read_bytes())
        identity = digest.hexdigest() + '\n'
        marker = obj / '.autoconf-inputs'
        if not (view / 'configure').is_file() or not marker.is_file() or marker.read_text() != identity:
            temporary = view / 'configure.new'
            autoconf_env = os.environ.copy()
            for name in ('AUTOM4TE', 'AUTOM4TE_CFG', 'AUTOM4TE_NO_FATAL',
                         'M4', 'PERL5LIB', 'PERL5OPT', 'autom4te_perllibdir'):
                autoconf_env.pop(name, None)
            subprocess.run([str(autoconf), '-i', '-o', str(temporary)],
                           cwd=view, env=autoconf_env, check=True)
            temporary.replace(view / 'configure')
            marker.write_text(identity)
        # Configure consumes these upstream templates and support inputs even
        # when the generated configure script itself is byte-for-byte equal.
        config_inputs = (
            'config.h.in', 'Makefile.in', 'buildpkg.sh.in', 'opensshd.init.in',
            'openssh.xml.in', 'openbsd-compat/Makefile.in',
            'openbsd-compat/regress/Makefile.in', 'survey.sh.in', '.depend',
            'config.guess', 'config.sub', 'ssh.c', 'ssh.1',
        )
        configured = hashlib.sha256()
        for name in config_inputs:
            for value in (os.fsencode(name), (source / name).read_bytes()):
                configured.update(len(value).to_bytes(8, 'big'))
                configured.update(value)
        # Configure links these selected target libraries when testing Kerberos.
        # Track replacements and in-place changes without reading whole libraries.
        for path in args.target_lib:
            try:
                resolved = path.resolve(strict=True)
                st = resolved.stat()
            except FileNotFoundError:
                fields = (str(path), 'missing')
            else:
                fields = (str(path), str(resolved), str(st.st_dev),
                          str(st.st_ino), str(st.st_size), str(st.st_mtime_ns),
                          str(st.st_ctime_ns))
            for field in fields:
                value = os.fsencode(field)
                configured.update(len(value).to_bytes(8, 'big'))
                configured.update(value)
        inputs = obj / '.configure-source-inputs'
        text = configured.hexdigest() + '\n'
        if not inputs.is_file() or inputs.read_text() != text:
            inputs.write_text(text)
        # A lost generated header must not be accepted as a warm configure.
        build = obj / 'build'
        if not (build / 'config.h').is_file() and (build / 'Makefile').is_file():
            (build / 'Makefile').unlink()
        # Recipes do not inherit bmake's jobserver descriptors.  Preserve
        # variable overrides, but run this single configure consumer serially.
        env = os.environ.copy()
        flags = env.get('MAKEFLAGS', '')
        flags = re.sub(r'(?<!\S)-J(?:[ \t]+[0-9]+,[0-9]+|[0-9]+,[0-9]+)(?=\s|$)', '', flags)
        flags = re.sub(r'(?<!\S)-j(?:[ \t]+[0-9]+|[0-9]+)(?=\s|$)', '', flags)
        env['MAKEFLAGS'] = flags
        subprocess.run([str(make), '-j1', '-C', str(args.makefile_dir),
                        'OPENSSH_CONFIG_OBJECT=' + str(obj), 'openssh-target-config'],
                       env=env, check=True)


if __name__ == '__main__':
    main()
