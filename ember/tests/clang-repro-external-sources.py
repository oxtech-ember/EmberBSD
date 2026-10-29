#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted Clang MKREPRO external source regression.
"""Check that MKREPRO maps EmberBSD external roots to canonical paths.

The test uses the real share/mk files, the lib/libc tags rules, bmake and
Clang. It compiles __FILE__ probes below two different host roots from the
object directory, as a build does. The objects must not contain a host root
and must be byte-identical.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOTS = (('EMBER_LIBEDIT_SOURCE', 'libedit', '/usr/src/external/libedit'),
         ('EMBER_LIBEVENT_SOURCE', 'libevent', '/usr/src/external/libevent'),
         ('EMBER_LIBARCHIVE_SOURCE', 'libarchive', '/usr/src/external/libarchive'),
         ('EMBER_BYACC_SOURCE', 'byacc', '/usr/src/external/byacc'),
         ('EMBER_TEXINFO_SOURCE', 'texinfo', '/usr/src/external/texinfo'),
         ('EMBER_RCS_SOURCE', 'rcs', '/usr/src/external/rcs'),
         ('EMBER_DIFFUTILS_SOURCE', 'diffutils', '/usr/src/external/diffutils'),
         ('EMBER_GREP_SOURCE', 'grep', '/usr/src/external/grep'),
         ('EMBER_TDB_SOURCE', 'tdb', '/usr/src/external/tdb'),
         ('EMBER_TOP_SOURCE', 'top', '/usr/src/external/top'),
         ('EMBER_ISCSI_SOURCE', 'iscsi', '/usr/src/external/iscsi'),
         ('EMBER_LIBPCAP_SOURCE', 'libpcap', '/usr/src/external/libpcap'),
         ('EMBER_TCP_WRAPPERS_SOURCE', 'tcpwrappers', '/usr/src/external/tcp-wrappers'),
         ('EMBER_TCPDUMP_SOURCE', 'tcpdump', '/usr/src/external/tcpdump'),
         ('EMBER_PPP_SOURCE', 'ppp', '/usr/src/external/ppp'),
         ('EMBER_NTP_SOURCE', 'ntp', '/usr/src/external/ntp'),
         ('EMBER_LIBOPTS_SOURCE', 'libopts', '/usr/src/external/libopts'),
         ('EMBER_GETTEXT_SOURCE', 'gettext', '/usr/src/external/gettext'),
         ('EMBER_LIBXML2_SOURCE', 'libxml2', '/usr/src/external/libxml2'),
         ('EMBER_LLVM_SOURCE', 'llvm', '/usr/src/external/llvm'),
         ('EMBER_EXTERNAL_LLVM_RUNTIME', 'runtime', '/usr/pkg/llvm'))


def libc_tags_rules(os_root):
    text = (os_root / 'lib/libc/Makefile').read_text(encoding='utf-8')
    rules = [line for line in text.splitlines() if line.startswith(('REGEX_SPECIALS=', 'MKREPRO_SED'))]
    if len(rules) < 2:
        raise SystemExit('lib/libc/Makefile has no MKREPRO tags rules')
    return '\n'.join(rules)


def make_variables(make, os_root, fixture, work, root, names):
    variables = ['MKREPRO=yes', 'EMBER_COMPILER_FAMILY=clang', 'MAKECONF=/dev/null',
                 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'EXTERNAL_TOOLCHAIN=' + str(root / 'toolchain'),
                 'NETBSDSRCDIR=' + str(root / 'src'), 'NETBSDOBJDIR=' + str(root / 'obj'),
                 'DESTDIR=' + str(root / 'dest')]
    variables += [f'{var}={root / name}' for var, name, _ in ROOTS]
    query = [part for name in names for part in ('-V', '${' + name + '}')]
    result = subprocess.run([make, '-r', '-m', str(os_root / 'share/mk'), '-f', str(fixture), *variables, *query],
                            cwd=work, capture_output=True, text=True,
                            env={'PATH': str(Path(make).parent) + ':' + str(Path(make).parent.parent / 'gnu/bin')})
    if result.returncode != 0:
        raise SystemExit('make variable query failed:\n' + result.stderr)
    return result.stdout.splitlines()


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--make', required=True, help='bmake from the selected host prefix')
    cli.add_argument('--clang', required=True, help='Clang that supports the aarch64--netbsd target')
    cli.add_argument('--os', type=Path, default=Path(__file__).resolve().parents[2])
    args = cli.parse_args()
    os_root = args.os.resolve()
    with tempfile.TemporaryDirectory(prefix='ember-clang-repro-') as temporary:
        work = Path(temporary).resolve()
        fixture = work / 'repro.mk'
        fixture.write_text(f'.include "{os_root}/share/mk/bsd.own.mk"\n'
                           f'.include "{os_root}/share/mk/bsd.sys.mk"\n'
                           f'{libc_tags_rules(os_root)}\n.PHONY: all\nall:\n')
        outputs = []
        for index in range(2):
            root = work / f'host-{index}' / ('deep' * (index + 1))
            for _, name, _ in ROOTS:
                (root / name / 'include').mkdir(parents=True)
            for directory in ('src', 'obj', 'dest', 'toolchain'):
                (root / directory).mkdir()
            reproflags, external, sed = make_variables(args.make, os_root, fixture, work, root,
                                                       ('REPROFLAGS', 'REPRO_EXTERNAL_ROOTS', 'MKREPRO_SED'))
            mapped = dict(item.split('=', 1) for item in external.split())
            for var, name, canonical in ROOTS:
                if mapped.get(str(root / name)) != canonical:
                    raise SystemExit(f'{var} has no canonical {canonical} map')
            (root / 'runtime/include/runtime.h').write_text('static const char runtime_name[] = __FILE__;\n')
            objects = []
            for var, name, canonical in ROOTS[:-1]:
                source = root / name / 'probe.c'
                source.write_text('#include <runtime.h>\n'
                                  'const char *probe(void) { return __FILE__; }\n'
                                  'const char *header(void) { return runtime_name; }\n')
                output = work / f'{name}-{index}.o'
                subprocess.run([args.clang, '--target=aarch64--netbsd', '-ffreestanding', '-nostdinc',
                                '-isystem', str(root / 'runtime/include'), '-O1', '-g', *shlex.split(reproflags),
                                '-c', str(source), '-o', str(output)], check=True, cwd=root / 'obj')
                data = output.read_bytes()
                if str(work).encode() in data:
                    leaked = [s for s in data.split(b'\0') if str(work).encode() in s]
                    raise SystemExit(f'{var}: a host build root leaked into {output.name}: {leaked[:2]!r}')
                for expected in (canonical + '/probe.c', '/usr/pkg/llvm/include/runtime.h'):
                    if expected.encode() + b'\0' not in data:
                        raise SystemExit(f'{var}: {expected} is not the __FILE__ value')
                objects.append(data)
            tags = '\n'.join(f'probe {root / name}/probe.c /^probe$/' for _, name, _ in ROOTS) + '\n'
            mapped_tags = subprocess.run(['sed', *shlex.split(sed)], input=tags, capture_output=True,
                                         text=True, check=True).stdout
            if str(work) in mapped_tags or any(canonical + '/probe.c' not in mapped_tags for _, _, canonical in ROOTS):
                raise SystemExit('libc tags keep a host external source root:\n' + mapped_tags)
            outputs.append((objects, mapped_tags))
        if outputs[0] != outputs[1]:
            raise SystemExit('objects or tags differ between two host roots')
    print('PASS: Clang MKREPRO maps common source and runtime roots; '
          'identical objects and libc tags from two host roots')


if __name__ == '__main__':
    main()
