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
import posixpath
import shlex
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
MAKE = None
HEADER_DIRS = ('include', 'sys', 'lib/libpthread', 'lib/libedit', 'lib/libossaudio')


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

    def test_bootstrap_stages_and_source_marker(self):
        for goal, objects, update in (('bootstrap-headers', 'yes', 'yes'),
                                      ('bootstrap-crt', 'yes', 'no'),
                                      ('bootstrap-crt', 'no', 'yes'),
                                      ('bootstrap-c', 'yes', 'no'),
                                      ('bootstrap-c', 'no', 'yes')):
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
                if goal in ('bootstrap-crt', 'bootstrap-c'):
                    if objects == 'yes':
                        expected.append(['lib/csu', 'obj'])
                    if update == 'no':
                        expected.append(['lib/csu', 'cleandir'])
                    expected += [['lib/csu', 'dependall'], ['lib/csu', 'install']]
                if goal == 'bootstrap-c':
                    if objects == 'yes':
                        expected.append(['lib/libc', 'obj'])
                    if update == 'no':
                        expected.append(['lib/libc', 'cleandir'])
                    expected += [['lib/libc', 'dependall'], ['lib/libc', 'install']]
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

    def test_c_failure_does_not_install_libc(self):
        self.env['FAIL_CALL'] = 'lib/libc dependall'
        result = self.stage('bootstrap-c')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls()[-1], ['lib/libc', 'dependall'])
        self.assertNotIn(['lib/libc', 'install'], self.calls())

    def test_riscv32_clang_selects_compiler_rt_without_changing_gcc(self):
        for arch, family, expected in (('riscv32', 'clang', 'no'),
                                       ('riscv32', 'gcc', 'yes'),
                                       ('riscv64', 'clang', 'yes'),
                                       ('x86_64', 'clang', 'no')):
            with self.subTest(arch=arch, family=family):
                machine = 'amd64' if arch == 'x86_64' else 'riscv'
                result = self.make(self.work, '-f', str(SOURCE / 'Makefile'),
                                   'MACHINE=' + machine, 'MACHINE_ARCH=' + arch,
                                   'EMBER_COMPILER_FAMILY=' + family, '-V', '${HAVE_LIBGCC}')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stdout.strip(), expected)

    def test_libc_uses_generated_dependencies_and_common_runtime_source(self):
        llvm = self.work / 'llvm'
        for name in ('runtimes/CMakeLists.txt', 'compiler-rt/lib/builtins/int_lib.h',
                     'compiler-rt/lib/builtins/comparetf2.c',
                     'compiler-rt/include/profile/InstrProfData.inc',
                     'libunwind/include/unwind.h', 'libunwind/include/__libunwind_config.h',
                     'libunwind/include/unwind_itanium.h', 'libunwind/include/unwind_arm_ehabi.h'):
            path = llvm / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture\n')
        archive = self.work / 'libunwind.a'
        archive.touch()
        result = self.make(SOURCE / 'lib/libc', 'MACHINE=riscv', 'MACHINE_ARCH=riscv32',
                           'EMBER_LLVM_SOURCE=' + str(llvm),
                           'EMBER_LIBUNWIND_ARCHIVE=' + str(archive),
                           'MKPROFILE=no', 'MKDEBUGLIB=no', 'MKSOFTFLOAT=yes',
                           '-V', '${DPSRCS}', '-V', '${SRCS}', '-V', '${COMPILER_RT_SRCDIR}')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        deps, sources, runtime = result.stdout.splitlines()
        self.assertIn('assym.h', deps.split())
        self.assertIn('__setjmp14.S', sources.split())
        self.assertIn('_setjmp.S', sources.split())
        self.assertEqual(runtime, str(llvm / 'compiler-rt'))

    def test_riscv32_softfloat_make_flags_select_integer_abi(self):
        result = self.make(self.work, '-f', str(SOURCE / 'Makefile'),
                           'MACHINE=riscv', 'MACHINE_ARCH=riscv32', 'MKSOFTFLOAT=yes',
                           '-V', '${CPUFLAGS}')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('-march=rv32imac_zicsr', result.stdout.split())
        self.assertIn('-mabi=ilp32', result.stdout.split())

    def test_dependency_recipes_keep_selected_abi_for_all_languages(self):
        fixture = self.work / 'dependencies.mk'
        fixture.write_text('SRCS= cprobe.c aprobe.S cxxprobe.cc objcprobe.m\n'
                           '.include <bsd.own.mk>\n.include <bsd.sys.mk>\n.include <bsd.dep.mk>\n')
        for name in ('cprobe.c', 'aprobe.S', 'cxxprobe.cc', 'objcprobe.m'):
            (self.work / name).touch()
        result = self.make(self.work, '-f', str(fixture), '-n',
                           'MACHINE=riscv', 'MACHINE_ARCH=riscv32', 'MKSOFTFLOAT=yes',
                           'CPUFLAGS=-march=rv32imac_zicsr -mabi=ilp32 -mllvm -inline-threshold=0',
                           'MKDEP=selected-mkdep', 'MKDEPCXX=selected-mkdep',
                           'MKDEPOBJC=selected-mkdep',
                           'COPTS.cprobe.c=-mabi=ilp32d', 'CPUFLAGS.cprobe.c=-mabi=ilp32',
                           'AFLAGS.aprobe.S=-mabi=ilp32d', 'CPUFLAGS.aprobe.S=-mabi=ilp32',
                           'COPTS.cxxprobe.cc=-mabi=ilp32d', 'CPUFLAGS.cxxprobe.cc=-mabi=ilp32',
                           'OBJCOPTS.objcprobe.m=-mabi=ilp32d', 'CPUFLAGS.objcprobe.m=-mabi=ilp32',
                           'cprobe.d', 'aprobe.d',
                           'cxxprobe.d', 'objcprobe.d')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = [shlex.split(line) for line in result.stdout.splitlines()
                    if line.startswith('selected-mkdep ')]
        self.assertEqual(len(commands), 4, result.stdout)
        for command in commands:
            self.assertIn('-march=rv32imac_zicsr', command)
            self.assertIn('-mabi=ilp32', command)
            self.assertEqual(command[command.index('-mllvm') + 1], '-inline-threshold=0')
            self.assertIn('-mabi=ilp32d', command)
            if 'objcprobe.m' in command:
                self.assertGreater(command.index('-mabi=ilp32d'), command.index('-mabi=ilp32'))
            else:
                self.assertGreater(len(command) - 1 - command[::-1].index('-mabi=ilp32'),
                                   command.index('-mabi=ilp32d'))
        result = self.make(self.work, '-f', str(fixture), '-n',
                           'MACHINE=riscv', 'MACHINE_ARCH=riscv32', 'MKSOFTFLOAT=yes',
                           'OBJCOPTS.objcprobe.m=-mabi=ilp32d', 'CPUFLAGS.objcprobe.m=-mabi=ilp32',
                           'objcprobe.o')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        command = next(shlex.split(line) for line in result.stdout.splitlines()
                       if ' -c ' in line and 'objcprobe.m' in line)
        self.assertGreater(command.index('-mabi=ilp32d'), command.index('-mabi=ilp32'))

    def test_host_dependency_recipe_does_not_receive_target_abi(self):
        result = self.make(SOURCE / 'tools/hostname', '-n',
                           'MACHINE=riscv', 'MACHINE_ARCH=riscv32', 'MKSOFTFLOAT=yes',
                           'HOST_MKDEP=selected-host-mkdep',
                           'CPUFLAGS.hostname.c=-mabi=ilp32', 'hostname.d')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = [shlex.split(line) for line in result.stdout.splitlines()
                    if line.startswith('selected-host-mkdep ')]
        self.assertEqual(len(commands), 1, result.stdout)
        self.assertFalse(any(arg.startswith(('-march=', '-mabi=')) for arg in commands[0]))

        fixture = self.work / 'hostlib.mk'
        fixture.write_text('HOSTLIB=fixture\nSRCS=hprobe.c\n.include <bsd.hostlib.mk>\n')
        (self.work / 'hprobe.c').touch()
        result = self.make(self.work, '-f', str(fixture), '-n',
                           'MACHINE=riscv', 'MACHINE_ARCH=riscv32', 'MKSOFTFLOAT=yes',
                           'HOST_MKDEP=selected-host-mkdep',
                           'CPUFLAGS.hprobe.c=-mabi=ilp32', 'hprobe.d')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = [shlex.split(line) for line in result.stdout.splitlines()
                    if line.startswith('selected-host-mkdep ')]
        self.assertEqual(len(commands), 1, result.stdout)
        self.assertFalse(any(arg.startswith(('-march=', '-mabi=')) for arg in commands[0]))

    def test_actual_header_graph_has_no_compiler_runtime(self):
        result = self.stage('bootstrap-headers')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        pending = [SOURCE / row[0] for row in self.calls() if row[1:] == ['includes']]
        visited = set()
        headers = set()
        installed = set()
        links = {}
        destination = str(self.work / 'dest')
        while pending:
            directory = pending.pop().resolve()
            if directory in visited:
                continue
            visited.add(directory)
            result = self.make(directory, '-V', '${INCS}', '-V', '${DEPINCS}',
                               '-V', '${__REALSUBDIR}',
                               '-V', '${.ALLTARGETS:M${DESTDIR}/usr/include/*}',
                               '-V', '${INCSYMLINKS}', 'includes')
            self.assertEqual(result.returncode, 0, str(directory) + result.stderr)
            incs, generated, children, targets, symlinks = result.stdout.splitlines()
            headers.update((incs + ' ' + generated).split())
            installed.update(path.removeprefix(destination) for path in targets.split())
            pairs = symlinks.split()
            self.assertEqual(len(pairs) % 2, 0)
            links.update((name, target) for target, name in zip(pairs[::2], pairs[1::2]))
            pending.extend(directory / child for child in children.split() if child != '.WAIT')
        # Resolve native header links against the actual selected install graph.
        # In particular sys/soundcard.h must have its libossaudio-owned target.
        self.assertTrue(links)
        for name in links:
            target, seen = name, set()
            while True:
                parts = target.split('/')[1:]
                prefixes = [('/' + '/'.join(parts[:i]), parts[i:])
                            for i in range(1, len(parts) + 1)]
                hop = next(((prefix, rest) for prefix, rest in prefixes
                            if prefix in links), None)
                if hop is None:
                    break
                self.assertNotIn(target, seen, 'header symlink cycle: ' + name)
                seen.add(target)
                prefix, rest = hop
                target = posixpath.normpath(posixpath.join(
                    posixpath.dirname(prefix), links[prefix], *rest))
            self.assertTrue(target in installed,
                            'missing header link target: ' + name + ' -> ' + target)
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
