#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Query the real helper make graph. Synthetic inputs are never compiled."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--make', required=True, type=Path)
options, remaining = parser.parse_known_args()
SOURCE = Path(__file__).resolve().parents[2]


class HostBFDTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='host-bfd-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.config = self.root / 'host-bfd.mk'
        self.source, self.build = self.root / 'src', self.root / 'build'
        for relative in ('src/include/elf/common.h', 'src/include/elf/external.h',
                         'src/include/ansidecl.h', 'src/include/symcat.h',
                         'build/bfd/bfd.h', 'build/bfd/.libs/libbfd.a',
                         'build/libiberty/libiberty.a'):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('synthetic input; never compile\n')
        self.fields = dict(TRIPLET='aarch64--netbsd', SOURCE=str(self.source), BUILD=str(self.build),
            CPPFLAGS=f'-I{self.build}/bfd -I{self.source}/include',
            LDADD=f'{self.build}/bfd/.libs/libbfd.a {self.build}/libiberty/libiberty.a -lz',
            DPADD=f'{self.build}/bfd/.libs/libbfd.a {self.build}/libiberty/libiberty.a')
        self.write_config()

    def write_config(self):
        self.config.write_text(''.join('EMBER_HOST_BFD_' + name + '= ' + value + '\n'
                                      for name, value in self.fields.items()))

    def query(self, directory, variables, selected=True):
        command = [str(options.make), '-C', str(SOURCE / directory),
                   'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
                   'EXTERNAL_TOOLCHAIN=/selected/compiler', 'TOOLCHAIN_MISSING=yes']
        if selected:
            command.append('EMBER_HOST_BFD_CONFIG=' + str(self.config))
        for variable in variables:
            command.extend(('-V', '${' + variable + '}'))
        return subprocess.run(command, text=True, capture_output=True)

    def test_external_graph_uses_common_bfd_without_old_provider(self):
        result = self.query('tools', ['TOOLCHAIN_BITS'])
        self.assertEqual(result.returncode, 0, result.stderr)
        words = result.stdout.split()
        self.assertIn('dbsym', words)
        self.assertIn('mdsetimage', words)
        self.assertNotIn('binutils', words)
        self.assertNotIn('gmake', words)

    def test_both_helpers_use_common_headers_and_static_dependencies(self):
        for helper in ('dbsym', 'mdsetimage'):
            result = self.query('tools/' + helper, ['CPPFLAGS', 'LDADD', 'DPADD'])
            self.assertEqual(result.returncode, 0, result.stderr)
            flags, libraries, dependencies = result.stdout.splitlines()
            self.assertIn('-I' + str(self.build / 'bfd'), flags)
            self.assertIn('-I' + str(self.source / 'include'), flags)
            self.assertNotIn('external/gpl3/binutils', flags)
            self.assertNotIn('obj/tools/binutils', flags + libraries + dependencies)
            for archive in self.fields['DPADD'].split():
                self.assertIn(archive, libraries)
                self.assertIn(archive, dependencies)

    def test_explicit_invalid_input_is_not_a_fallback(self):
        for name, value in [('TRIPLET', 'x86_64--netbsd'), ('CPPFLAGS', ''),
                            ('DPADD', str(self.root / 'absent.a'))]:
            old = self.fields[name]
            self.fields[name] = value
            self.write_config()
            for directory in ('tools', 'tools/dbsym', 'tools/mdsetimage'):
                result = self.query(directory, ['TOOLCHAIN_BITS'])
                self.assertNotEqual(result.returncode, 0, (name, directory, result.stdout))
                self.assertIn('BFD', result.stderr)
            self.fields[name] = old
        self.config.unlink()
        self.assertNotEqual(self.query('tools', ['TOOLCHAIN_BITS']).returncode, 0)

    def test_unselected_legacy_graph_is_unchanged(self):
        result = self.query('tools', ['TOOLCHAIN_BITS'], selected=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('gmake', result.stdout.split())
        self.assertIn('binutils', result.stdout.split())
        result = self.query('tools/dbsym', ['CPPFLAGS'], selected=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('external/gpl3/binutils', result.stdout)


unittest.main(argv=[sys.argv[0], *remaining])
