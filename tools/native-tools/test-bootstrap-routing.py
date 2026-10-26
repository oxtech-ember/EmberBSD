#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Run early native configure and make queries with only OS-routed commands.

The compiler fixture accepts only --version. No compilation or install runs.
Test utility providers are selected explicitly, separately from a build prefix.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
OPTIONS = None


class BootstrapRoutingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='ember-bootstrap-routing-')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name).resolve()
        self.routing = self.work / 'routing'
        self.routing.mkdir()
        compiler = self.work / 'compiler'
        compiler.write_text('#!' + str(OPTIONS.shell) + '\n'
                            'if test "$#" = 1 && test "$1" = --version; then\n'
                            '  echo "clang version fixture"; exit 0\n'
                            'fi\nexit 73\n')
        compiler.chmod(0o755)
        for row in OPTIONS.contract.read_text().splitlines()[1:]:
            command, role, _ = row.split()
            if role in ('cc', 'cxx', 'cpp', 'as', 'ar', 'ld', 'ranlib', 'yacc'):
                tool = compiler
            elif role in ('sh', 'make'):
                tool = OPTIONS.shell if role == 'sh' else OPTIONS.make
            else:
                tool = shutil.which(command, path=OPTIONS.utility_path)
                self.assertIsNotNone(tool, 'missing explicit test utility: ' + command)
            (self.routing / command).symlink_to(tool)
        self.compiler = compiler
        self.env = {'PATH': str(self.routing), 'LC_ALL': 'C',
                    'MAKESYSPATH': str(SOURCE / 'share/mk'),
                    'CONFIG_SHELL': str(OPTIONS.shell)}

    def run_command(self, command, directory):
        return subprocess.run([str(arg) for arg in command], cwd=directory,
                              env=self.env, text=True, capture_output=True, timeout=30)

    def test_real_makefiles_detect_clang(self):
        for directory in ('compat', 'gmake'):
            with self.subTest(directory=directory):
                result = self.run_command([
                    OPTIONS.make, '-m', SOURCE / 'share/mk', 'MAKECONF=/dev/null',
                    '_SRC_TOP_=', 'MACHINE=amd64', 'MACHINE_ARCH=x86_64',
                    'NETBSDSRCDIR=' + str(SOURCE), 'TOOLDIR=' + str(self.work / 'tools'),
                    'DESTDIR=' + str(self.work / 'dest'), 'USETOOLS=yes',
                    'HOST_SH=' + str(OPTIONS.shell), 'HOST_CC=' + str(self.compiler),
                    'HOST_YACC=' + str(self.compiler), '-V', '${HOST_COMPILER_CLANG}'],
                    SOURCE / 'tools' / directory)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stdout, 'yes\n', result.stderr)
                self.assertEqual(result.stderr, '')

    def test_real_configure_parses_options_before_compilation(self):
        for directory in ('host-mkdep', 'compat'):
            with self.subTest(directory=directory):
                result = self.run_command([OPTIONS.shell, SOURCE / 'tools' / directory / 'configure',
                                           '--prefix=/explicit-test-prefix', '--help'], self.work)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('--prefix=PREFIX', result.stdout)
                self.assertEqual(result.stderr, '')

    def test_real_configure_reaches_selected_compiler_failure(self):
        for directory in ('host-mkdep', 'compat'):
            with self.subTest(directory=directory):
                work = self.work / directory
                work.mkdir()
                self.env['CC'] = str(self.compiler)
                result = self.run_command([OPTIONS.shell, SOURCE / 'tools' / directory / 'configure'], work)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('C compiler cannot create executables', result.stdout + result.stderr)
                log = (work / 'config.log').read_text()
                # Autoconf 2.52 deliberately fails this PATH-separator probe.
                log = '\n'.join(line for line in log.splitlines() if ': conftest.sh: ' not in line)
                self.assertNotRegex(result.stdout + result.stderr + log, r'(?i)(?:command )?not found')
                self.assertIn(str(self.compiler), log)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--make', required=True, type=Path)
    parser.add_argument('--shell', required=True, type=Path)
    parser.add_argument('--utility-path', required=True,
                        help='Explicit search path for test utility providers, never a build fallback')
    parser.add_argument('--contract', type=Path, default=SOURCE / 'tools/native-tools/contract',
                        help='Use an older contract for a causal negative control')
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.make = OPTIONS.make.resolve(strict=True)
    OPTIONS.shell = OPTIONS.shell.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
