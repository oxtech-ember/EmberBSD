#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""The shared console profile selects real native OS build targets."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).with_name('static-profile.py')
spec = importlib.util.spec_from_file_location('static_profile', path)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)

class ConsoleProfile(unittest.TestCase):
    def test_console_profile_has_target_abi_and_minimum_native_components(self):
        selected = profile.console('riscv32')
        self.assertEqual((selected['machine'], selected['arch'], selected['triplet']),
                         ('riscv', 'riscv32', 'riscv32--netbsd'))
        self.assertEqual(selected['headers'], ('lib/libcurses',))
        self.assertEqual(selected['libraries'], ('lib/libterminfo', 'lib/libedit', 'lib/libutil'))
        self.assertEqual(selected['programs'], ('bin/sh', 'sbin/init'))
        self.assertIn('MKSOFTFLOAT=yes', selected['flags'])
        self.assertEqual(selected['elf_class'], 'ELF32')
    def test_foundation_rejects_abi_and_runtime_mismatch(self):
        record = {'format': 'emberbsd-llvm-os-stage', 'phase': 'base',
                  'signature': {'target': {'arch': 'riscv32', 'triplet': 'riscv32--netbsd'},
                                'flags': ['MKSOFTFLOAT=yes', 'MKPIC=no', 'MKPICINSTALL=no', 'MKCOMPAT=no', 'MKCXX=no', 'MKPIE=no', 'LDSTATIC=-static'],
                                'runtime': {'manifest': {'identity': 'selected'}}}}
        profile.check_foundation(record, 'riscv32', {'identity': 'selected'})
        for change in ('runtime', 'abi', 'flags', 'conflict'):
            import copy
            invalid = copy.deepcopy(record)
            if change == 'runtime': invalid['signature']['runtime']['manifest'] = {'identity': 'other'}
            if change == 'abi': invalid['signature']['target']['arch'] = 'riscv64'
            if change == 'flags': invalid['signature']['flags'].remove('MKSOFTFLOAT=yes')
            if change == 'conflict': invalid['signature']['flags'].append('MKSOFTFLOAT=no')
            with self.assertRaises(ValueError):
                profile.check_foundation(invalid, 'riscv32', {'identity': 'selected'})

    def test_unimplemented_target_is_rejected(self):
        with self.assertRaises(ValueError):
            profile.console('riscv64')

    def test_console_rejects_non_llvm_target_compiler(self):
        profile.check_compiler_family('clang')
        for family in ('gcc', 'other', ''):
            with self.assertRaisesRegex(ValueError, 'clang'):
                profile.check_compiler_family(family)

    def test_console_cli_rejects_gcc_before_any_build(self):
        import os
        import subprocess
        import sys
        env = dict(os.environ, EMBER_USERLAND_PROFILE='console', EMBER_TARGET_COMPILER_FAMILY='gcc')
        result = subprocess.run([sys.executable, '-B', str(path.with_name('cix-p1-native.py')),
                                 '/source', '/output', '/tools', '/compiler'], env=env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('supports only the LLVM C foundation', result.stderr)

    def test_actual_os_tools_checked_after_relocation(self):
        from types import SimpleNamespace
        seen = []
        native = SimpleNamespace(host={'os': 'Darwin', 'cpu': 'arm64'})
        profile.check_os_tools(Path('/relocated/tools'), native, '/selected/bin',
                               lambda *args, **kw: seen.append((args, kw)))
        self.assertEqual([row[0][1] for row in seen], [
            '/relocated/tools/bin/nbmkdep', '/relocated/tools/bin/nbmtree',
            '/relocated/tools/bin/nbmakefs'])
        self.assertTrue(all(row[0][0] == '/relocated/tools' and
                            row[0][2] == native.host and
                            row[1]['search_path'] == '/selected/bin' for row in seen))

    def test_compiler_relocation_preserves_identity_but_changed_abi_does_not(self):
        import copy
        roles = ('cc', 'cxx', 'cpp', 'as', 'ld', 'ar', 'ranlib', 'nm',
                 'strip', 'objcopy', 'objdump', 'readelf', 'size', 'strings')
        original = {'family': 'clang', 'version': '23.1.4', 'triplet': 'riscv32--netbsd',
                    'reported_triplet': 'riscv32-unknown-netbsd', 'resource_dir': '/old/lib',
                    'binaries': {r: {'path': '/old/' + r, 'sha256': 'hash-' + r} for r in roles}}
        relocated = copy.deepcopy(original)
        relocated['resource_dir'] = '/new/lib'
        for role in roles:
            relocated['binaries'][role]['path'] = '/new/' + role
        relocated['reported_triplet'] = 'riscv32--netbsd'
        profile.check_compiler_identity(relocated, original)
        relocated['triplet'] = 'riscv64--netbsd'
        with self.assertRaisesRegex(ValueError, 'compiler differs'):
            profile.check_compiler_identity(relocated, original)

    def test_unknown_profile_is_rejected_before_dispatch(self):
        import os
        import subprocess
        import sys
        env = dict(os.environ, EMBER_USERLAND_PROFILE='typo')
        result = subprocess.run([sys.executable, '-B', str(path.with_name('cix-p1-native.py'))],
                                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsupported userland profile', result.stderr)

    def test_dirty_source_provenance_tracks_content_not_just_status(self):
        import os
        import shutil
        import subprocess
        import tempfile
        git = os.environ.get('EMBER_GIT') or shutil.which('git')
        self.assertIsNotNone(git)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            def run(*args):
                subprocess.run([git, '-C', str(root), *args], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            run('init', '-q')
            (root / 'tracked').write_text('original')
            run('add', 'tracked')
            run('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'fixture')
            clean = profile.source_worktree_state(git, root)
            self.assertEqual(clean['state'], 'clean')
            (root / 'tracked').write_text('first')
            (root / 'untracked').write_text('one')
            first = profile.source_worktree_state(git, root)
            self.assertEqual(first['state'], 'modified')
            (root / 'untracked').write_text('two')
            second = profile.source_worktree_state(git, root)
            self.assertNotEqual(first['sha256'], second['sha256'])
            (root / 'tracked').write_text('second')
            self.assertNotEqual(second['sha256'], profile.source_worktree_state(git, root)['sha256'])
            run('add', 'tracked')
            staged = profile.source_worktree_state(git, root)
            self.assertEqual(staged['state'], 'modified')
            self.assertNotEqual(staged['sha256'], second['sha256'])
            (root / 'untracked').unlink()
            (root / 'untracked').symlink_to('first-target')
            link = profile.source_worktree_state(git, root)
            (root / 'untracked').unlink()
            (root / 'untracked').symlink_to('second-target')
            self.assertNotEqual(link['sha256'], profile.source_worktree_state(git, root)['sha256'])

    def test_console_builder_uses_selected_python(self):
        import os
        import subprocess
        import sys
        import tempfile
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            marker = directory / 'selected'
            python = directory / 'selected-python'
            python.write_text(f'#!{sys.executable}\nfrom pathlib import Path\nPath({str(marker)!r}).touch()\nraise SystemExit(47)\n')
            python.chmod(0o755)
            shell = os.environ.get('TEST_SH')
            if not shell or not os.path.isabs(shell):
                self.fail('Set TEST_SH to the selected native shell')
            env = dict(os.environ, HOST_SH=shell)
            args = [env['HOST_SH'], str(path.with_name('cix-p1-build-userland.sh')), '--console-selected',
                    '/src', str(directory), '/tools', '/compiler', '/sdk', '/prefix', 'clang',
                    'riscv', 'riscv32', '/libedit', '/llvm', '/bfd', '/byacc',
                    '/runtime', '/resource', '/unwind', '/compiler-resource', '2', '1', 'revision',
                    'lib/libcurses', 'lib/libedit', 'bin/sh', str(python)]
            result = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 47, result.stderr)
            self.assertTrue(marker.is_file())

if __name__ == '__main__':
    unittest.main()
