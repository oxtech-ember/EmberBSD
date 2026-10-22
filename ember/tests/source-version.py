#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted source-version propagation regression.
"""Evaluate real kernel make rules without building target objects."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bmake', type=Path, help='host BSD make executable')
    args = parser.parse_args()
    bmake = args.bmake.resolve(strict=True)
    source = Path(__file__).resolve().parents[2]
    own = source / 'share/mk/bsd.own.mk'
    original = own.read_bytes()
    definition = rb'(?m)^(EMBER_VERSION\?=[ \t]*)([^\s#]+)'
    values = re.findall(definition, original)
    if len(values) != 1:
        raise RuntimeError('expected one canonical source version')
    default = values[0][1].decode('ascii')
    environment = {key: os.environ[key] for key in ('PATH', 'HOME', 'TMPDIR') if key in os.environ}
    environment.update(MAKECONF='/dev/null', LC_ALL='C')
    with tempfile.TemporaryDirectory(prefix='ember-source-version-') as temporary:
        work = Path(temporary)
        includes = work / 'share/mk'
        includes.parent.mkdir()
        kernel = work / 'sys'
        kernel.mkdir()
        for child in (source / 'sys').iterdir():
            (kernel / child.name).symlink_to(child)
        (work / 'common').symlink_to(source / 'common')
        shutil.copytree(source / 'share/mk', includes)
        fixture = work / 'Makefile'
        for machine, architecture in (('amd64', 'x86_64'), ('evbarm', 'aarch64')):
            fixture.write_text(
                '.SUFFIXES: .c .S .s .o .a\n'
                f'MACHINE={machine}\nMACHINE_ARCH={architecture}\n'
                'USETOOLS=no\n_SKIP_DTS=yes\n'
                f'S={kernel}\n'
                '.include <bsd.own.mk>\n'
                '.include "${S}/conf/Makefile.kern.inc"\n')
            for version in (default, '2.7+fixture'):
                (includes / 'bsd.own.mk').write_bytes(re.sub(
                    definition, lambda match: match[1] + version.encode('ascii'), original))
                result = subprocess.run(
                    [str(bmake), '-r', '-m', str(includes), '-C', str(work),
                     '-V', '${EMBER_VERSION}', '-V', '${CPPFLAGS}'],
                    env=environment, text=True, capture_output=True, check=True)
                actual, flags = result.stdout.splitlines()
                if actual != version or f'-DEMBER_VERSION=\\"{version}\\"' not in flags:
                    raise RuntimeError('source version did not reach actual kernel CPPFLAGS')
                print(f'PASS: {machine} source {version} reaches kernel CPPFLAGS')
        with fixture.open('a') as stream:
            stream.write("init_main.o:\n\t@printf '%s\\n' '${EMBER_VERSION}' > ${.TARGET}\n")
        current = includes / 'bsd.own.mk'
        current.write_bytes(re.sub(definition, lambda match: match[1] + b'2.7+fixture', original))
        os.utime(current, (1000000000, 1000000000))
        command = [str(bmake), '-r', '-m', str(includes), '-C', str(work), 'init_main.o']
        subprocess.run(command, env=environment, text=True, capture_output=True, check=True)
        marker = work / 'init_main.o'
        os.utime(marker, (1000000001, 1000000001))
        subprocess.run(command, env=environment, text=True, capture_output=True, check=True)
        if marker.stat().st_mtime != 1000000001:
            raise RuntimeError('unchanged source version rebuilt the banner object')
        current.write_bytes(re.sub(definition, lambda match: match[1] + b'2.8+fixture', original))
        os.utime(current, (1000000002, 1000000002))
        subprocess.run(command, env=environment, text=True, capture_output=True, check=True)
        if marker.read_text().strip() != '2.8+fixture':
            raise RuntimeError('incremental make reused the previous product banner')
        print('PASS: canonical source version invalidates only the stale banner fixture')
        fixture.write_text(f'.SUFFIXES: .c .S .s .o .a\nS={source / "sys"}\n'
                           '.include "${S}/conf/Makefile.kern.inc"\n')
        result = subprocess.run([str(bmake), '-r', '-m', str(includes), '-C', str(work),
                                 '-V', '${EMBER_VERSION}'], env=environment,
                                text=True, capture_output=True)
        if result.returncode == 0 or 'Include source <bsd.own.mk>' not in result.stderr:
            raise RuntimeError('kernel silently accepted missing source version settings')
        print('PASS: missing canonical version fails before a kernel build')
    if own.read_bytes() != original:
        raise RuntimeError('test changed the actual source version')


if __name__ == '__main__':
    main()
