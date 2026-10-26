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
parser.add_argument('--install', type=Path, help='explicit installer for the bounded elfedit copy check')
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
        self.elfedit = self.root / 'prefix/bin/aarch64--netbsd-elfedit'
        self.elfedit.parent.mkdir(parents=True)
        self.elfedit.write_text('synthetic elfedit; never execute\n')
        self.compiler = self.root / 'compiler'
        self.compiler.write_text('#!/bin/sh\ncase "$1" in --version) echo "clang fixture";; *) exit 73;; esac\n')
        self.compiler.chmod(0o755)
        self.write_config()

    def write_config(self):
        self.config.write_text(''.join('EMBER_HOST_BFD_' + name + '= ' + value + '\n'
                                      for name, value in self.fields.items()))

    def query(self, directory, variables, selected=True, family='clang', targets=()):
        command = [str(options.make), '-m', str(SOURCE / 'share/mk'),
                   'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(SOURCE),
                   'TOOLDIR=' + str(self.root / 'tools'), 'DESTDIR=' + str(self.root / 'dest'),
                   'HOST_CC=' + str(self.compiler), 'HOST_CXX=' + str(self.compiler),
                   'HOST_YACC=' + str(self.compiler), 'HOST_OSTYPE=Darwin-fixture-arm64',
                   'MKMAN=no', 'MKINFO=no', 'MKCROSSGDB=no', 'USETOOLS=yes',
                   'EMBER_COMPILER_FAMILY=' + family, '-C', str(SOURCE / directory),
                   'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
                   'EXTERNAL_TOOLCHAIN=/selected/compiler', 'TOOLCHAIN_MISSING=yes']
        if selected:
            command.append('EMBER_HOST_BFD_CONFIG=' + str(self.config))
        for variable in variables:
            command.extend(('-V', '${' + variable + '}'))
        command.extend(targets)
        return subprocess.run(command, text=True, capture_output=True, timeout=30)

    def test_external_graph_uses_common_bfd_without_old_provider(self):
        result = self.query('tools', ['TOOLCHAIN_BITS'])
        self.assertEqual(result.returncode, 0, result.stderr)
        words = result.stdout.split()
        self.assertIn('dbsym', words)
        self.assertIn('mdsetimage', words)
        self.assertIn('binutils', words)
        adapter = self.query('tools/binutils', ['GNUHOSTDIST', 'MAKE_PROGRAM', 'EMBER_HOST_ELFEDIT'])
        self.assertEqual(adapter.returncode, 0, adapter.stderr)
        self.assertEqual(adapter.stdout.splitlines(), ['', '', str(self.elfedit)])
        gcc = self.query('tools', ['TOOLCHAIN_BITS'], family='gcc')
        self.assertEqual(gcc.returncode, 0, gcc.stderr)
        self.assertNotIn('binutils', gcc.stdout.split())
        self.assertNotIn('gmake', gcc.stdout.split())
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
            self.assertEqual(libraries.split(), self.fields['LDADD'].split())
            for archive in self.fields['DPADD'].split():
                self.assertIn(archive, libraries)
                self.assertIn(archive, dependencies)

    def test_existing_elfedit_install_target_uses_common_output(self):
        if options.install is None:
            self.skipTest('Select --install for the bounded actual install check')
        result = self.query('tools/binutils', [], targets=[
            'HOST_INSTALL_FILE=' + str(options.install), 'install'])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'tools/bin/aarch64--netbsd-elfedit').read_bytes(),
                         self.elfedit.read_bytes())
        self.elfedit.unlink()
        result = self.query('tools/binutils', ['EMBER_HOST_ELFEDIT'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing common host Binutils elfedit', result.stderr)

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

    def test_explicit_native_mapping_cannot_select_the_old_provider(self):
        for directory in ('tools', 'tools/binutils', 'tools/dbsym', 'tools/mdsetimage'):
            result = self.query(directory, ['TOOLCHAIN_BITS'], selected=False,
                                targets=['EMBER_NATIVE_TOOLS=/selected/native-tools', 'HOST_SH=/bin/sh'])
            self.assertNotEqual(result.returncode, 0, (directory, result.stdout))
            self.assertIn('EMBER_HOST_BFD_CONFIG', result.stderr)

    def test_unselected_legacy_graph_is_unchanged(self):
        result = self.query('tools', ['TOOLCHAIN_BITS'], selected=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('gmake', result.stdout.split())
        self.assertIn('binutils', result.stdout.split())
        result = self.query('tools/dbsym', ['CPPFLAGS'], selected=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('external/gpl3/binutils', result.stdout)


unittest.main(argv=[sys.argv[0], *remaining])
