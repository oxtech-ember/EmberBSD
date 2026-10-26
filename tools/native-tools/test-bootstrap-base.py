#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Check native bootstrap target flow and real header/CRT make metadata.

No compiler or installation runs. Recursive stage commands are recorded;
header ownership and CRT commands are evaluated from the actual Makefiles.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
MAKE = None
HEADER_DIRS = ('include', 'sys', 'lib/libpthread', 'lib/libedit')


class BootstrapBaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='ember-bootstrap-base-')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name).resolve()
        self.libedit = self.work / 'libedit'
        # Parse-only fixtures; they are not a prepared source or build result.
        for name in ('src/histedit.h', 'src/editline/readline.h', 'src/makelist',
                     'src/el.c', 'doc/editline.3.roff', 'ports.distinfo', 'libedit-source.json'):
            path = self.libedit / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture\n')
        self.log = self.work / 'calls.jsonl'
        recorder = self.work / 'record.py'
        recorder.write_text('import json, os, sys\n'
                            'with open(os.environ["CALL_LOG"], "a") as output:\n'
                            '    output.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                            'if " ".join(sys.argv[1:]) == os.environ.get("FAIL_CALL"):\n'
                            '    sys.exit(19)\n')
        self.env = dict(os.environ, CALL_LOG=str(self.log), LC_ALL='C')
        self.args = [str(MAKE), '-m', str(SOURCE / 'share/mk'), 'MAKECONF=/dev/null',
                     '_SRC_TOP_=', 'MACHINE=amd64', 'MACHINE_ARCH=x86_64',
                     'TOOLDIR=' + str(self.work / 'tools'), 'DESTDIR=' + str(self.work / 'dest'),
                     'NETBSDSRCDIR=' + str(SOURCE), 'HOST_OSTYPE=Darwin-fixture-arm64',
                     'EXTERNAL_TOOLCHAIN=/selected/llvm', 'USETOOLS=yes', 'MKMAN=no',
                     'MKDOC=no', 'MKHTML=no', 'MKINFO=no', 'MKX11=no', 'MKLINT=no',
                     'MKDTB=no', 'MKKMOD=no', 'MKRUMP=no',
                     'EMBER_LIBEDIT_SOURCE=' + str(self.libedit)]
        self.recorder = 'MAKEDIRTARGET=' + shlex.join([sys.executable, str(recorder)])

    def make(self, directory, *args):
        return subprocess.run([*self.args, *args], cwd=directory, env=self.env,
                              text=True, capture_output=True, timeout=30)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def stage(self, *args):
        return self.make(self.work, '-f', str(SOURCE / 'Makefile'), self.recorder, *args)

    def test_headers_then_only_crt_and_source_marker(self):
        for goal, objects, update in (('bootstrap-headers', 'yes', 'yes'),
                                      ('bootstrap-crt', 'yes', 'no'),
                                      ('bootstrap-crt', 'no', 'yes')):
            with self.subTest(goal=goal, objects=objects, update=update):
                self.log.unlink(missing_ok=True)
                result = self.stage('-j4', 'MKOBJDIRS=' + objects, 'MKUPDATE=' + update, goal)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                expected = ([['.', 'do-top-obj'], ['etc/mtree', 'obj']]
                            if objects == 'yes' else [])
                expected.append(['.', 'do-distrib-dirs'])
                for directory in HEADER_DIRS:
                    if objects == 'yes':
                        expected.append([directory, 'obj'])
                    expected.append([directory, 'includes'])
                expected.append(['lib/libedit', str(self.work / 'dest/usr/share/emberbsd/ports/editline.distinfo')])
                if goal == 'bootstrap-crt':
                    if objects == 'yes':
                        expected.append(['lib/csu', 'obj'])
                    if update == 'no':
                        expected.append(['lib/csu', 'cleandir'])
                    expected += [['lib/csu', 'dependall'], ['lib/csu', 'install']]
                self.assertEqual(self.calls(), expected)

    def test_refuses_missing_prepared_libedit_and_root_destination(self):
        for setting in ('EMBER_LIBEDIT_SOURCE=', 'DESTDIR=', 'DESTDIR=/'):
            with self.subTest(setting=setting):
                result = self.stage(setting, 'bootstrap-headers')
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.calls(), [])
        (self.libedit / 'ports.distinfo').unlink()
        result = self.stage('bootstrap-crt')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing prepared libedit input', result.stderr)
        self.assertEqual(self.calls(), [])

    def test_header_failure_stops_before_crt(self):
        self.env['FAIL_CALL'] = 'sys includes'
        result = self.stage('bootstrap-crt')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls()[-1], ['sys', 'includes'])
        self.assertFalse(any(row[0] == 'lib/csu' for row in self.calls()))

    def test_actual_header_graph_has_no_compiler_runtime(self):
        pending = [SOURCE / directory for directory in HEADER_DIRS]
        visited = set()
        headers = set()
        while pending:
            directory = pending.pop().resolve()
            if directory in visited:
                continue
            visited.add(directory)
            result = self.make(directory, '-V', '${INCS}', '-V', '${DEPINCS}',
                               '-V', '${__REALSUBDIR}', 'includes')
            self.assertEqual(result.returncode, 0, str(directory) + result.stderr)
            incs, generated, children = result.stdout.splitlines()
            headers.update((incs + ' ' + generated).split())
            pending.extend(directory / child for child in children.split() if child != '.WAIT')
        self.assertTrue({'stdio.h', 'types.h', 'pthread.h', 'histedit.h', 'readline.h'} <= headers)
        self.assertFalse({'unwind.h', 'unwind-arm.h', '__libunwind_config.h'} & headers)
        self.assertFalse(any('compiler_rt' in str(path) or 'libunwind' in str(path)
                             or path == SOURCE / 'lib/libc' for path in visited))

    def test_crt_only_compiles_and_relocatably_links(self):
        result = self.make(SOURCE / 'lib/csu', '-n', 'realall')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = [shlex.split(line) for line in result.stdout.splitlines()
                    if line.startswith('/selected/llvm/bin/')]
        self.assertTrue(commands)
        for command in commands:
            if command[0].endswith('/clang'):
                self.assertIn('-c', command)
                self.assertIn('--target=x86_64--netbsd', command)
            elif command[0].endswith('/ld.lld'):
                self.assertIn('-r', command)
            else:
                self.assertTrue(command[0].endswith('/llvm-objcopy'), command)
            self.assertFalse(any(arg.startswith('-l') for arg in command), command)
        self.assertIn('crtbegin.o', result.stdout)
        self.assertIn('crtend.o', result.stdout)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--make', required=True, type=Path)
    parser.add_argument('--source', type=Path, default=SOURCE)
    args, remaining = parser.parse_known_args()
    MAKE, SOURCE = args.make.resolve(), args.source.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
