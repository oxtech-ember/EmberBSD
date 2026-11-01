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
    def test_compact_config_registers_login_shell_alias(self):
        import os
        import subprocess
        import tempfile
        shell = os.environ.get('TEST_SH')
        if not shell:
            self.fail('Set TEST_SH to the selected native shell')
        source = path.with_name('cix-p1-build-userland.sh').read_text()
        start = source.index("        {\n            printf 'srcdirs bin sbin")
        end = source.index('        } > "$out/crunch/console-tools.conf"', start)
        block = source[start:end] + '        } > "$out/crunch/console-tools.conf"\n'
        with tempfile.TemporaryDirectory() as name:
            out = Path(name)
            (out / 'crunch').mkdir()
            subprocess.run([shell, '-c', 'set -eu\n' + block], check=True,
                           env=dict(os.environ, out=str(out), programs='bin/sh sbin/init'))
            config = (out / 'crunch/console-tools.conf').read_text().splitlines()
            self.assertIn('ln sh -sh', config)

    def test_compact_nodes_come_from_makedev_and_reject_duplicates(self):
        import os
        import subprocess
        import tempfile
        shell = os.environ.get('TEST_SH')
        if not shell:
            self.fail('Set TEST_SH to the selected native shell')
        awk = os.environ.get('TEST_AWK')
        if not awk:
            for name in ('nawk', 'awk'):
                candidate = Path(shell).parent / name
                if candidate.exists():
                    awk = str(candidate)
                    break
        if not awk:
            self.fail('Set TEST_AWK to the selected native awk')
        source = path.with_name('cix-p1-build-userland.sh').read_text()
        start = source.index('    if [ "$profile" = console-tools ]; then\n        MACHINE=')
        middle = source.index('    "$tools/bin/nbmtree"', start)
        select = source.index('    for node in $devices; do', middle)
        end = source.index('    "$tools/bin/nbmakefs"', select)
        block = source[start:middle] + source[select:end]
        with tempfile.TemporaryDirectory() as name:
            out = Path(name)
            (out / 'obj/etc').mkdir(parents=True)
            (out / 'routing').mkdir()
            (out / 'routing/awk').symlink_to(awk)
            makedev = out / 'obj/etc/MAKEDEV'
            makedev.write_text('test "$*" = "-s std wscons iic0" || exit 1\n'
                               'for n in console null zero tty klog ttyE0 wsmouse0 iic0; do\n'
                               '  printf "./%s type=char device=fixture mode=600\\n" "$n"\n'
                               'done\n'
                               'if [ "${DUPLICATE:-}" = 1 ]; then\n'
                               '  printf "./iic0 type=char device=fixture mode=600\\n"\n'
                               'fi\n')
            env = dict(os.environ, out=str(out), profile='console-tools', machine='riscv',
                       arch='riscv32', HOST_SH=shell,
                       PATH=str(out / 'routing') + os.pathsep + os.environ['PATH'])
            spec = out / 'root.spec'
            spec.write_text('')
            subprocess.run([shell, '-c', 'set -eu\n' + block], env=env, check=True)
            self.assertEqual(len(spec.read_text().splitlines()), 8)
            self.assertIn('./dev/iic0 type=char ', spec.read_text())
            spec.write_text('')
            result = subprocess.run([shell, '-c', 'set -eu\n' + block],
                                    env=dict(env, DUPLICATE='1'), capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Missing or duplicate device: iic0', result.stderr)

    def test_external_elf_staging_sets_executable_mode(self):
        import os
        import subprocess
        import tempfile
        import stat
        shell = os.environ.get('TEST_SH')
        if not shell:
            self.fail('Set TEST_SH to the selected native shell')
        source = path.with_name('cix-p1-build-userland.sh').read_text()
        start = source.index('    if [ -n "$extra" ]; then')
        end = source.index('    if [ -n "$overlay" ]; then', start)
        block = source[start:end]
        with tempfile.TemporaryDirectory() as name:
            base = Path(name)
            out, extra = base / 'out', base / 'test-app'
            (out / 'root/bin').mkdir(parents=True)
            extra.write_bytes(b'ELF fixture')
            extra.chmod(0o644)
            subprocess.run([shell, '-c', 'set -eu\n' + block],
                           env=dict(os.environ, out=str(out), extra=str(extra)), check=True)
            installed = out / 'root/bin/test-app'
            self.assertEqual(installed.read_bytes(), extra.read_bytes())
            self.assertEqual(stat.S_IMODE(installed.stat().st_mode), 0o555)
            self.assertEqual(stat.S_IMODE(extra.stat().st_mode), 0o644)

    def test_optional_overlay_does_not_replace_base_or_follow_links(self):
        import tempfile
        with tempfile.TemporaryDirectory() as name:
            base = Path(name)
            root, overlay = base / 'root', base / 'overlay'
            (root / 'bin').mkdir(parents=True)
            (root / 'bin/sh').write_text('base')
            (overlay / 'etc').mkdir(parents=True)
            (overlay / 'etc/config').write_text('private')
            profile.stage_overlay(overlay, root)
            self.assertEqual((root / 'etc/config').read_text(), 'private')
            (overlay / 'etc/config').unlink()
            (overlay / 'etc/link').symlink_to('/outside')
            with self.assertRaises(ValueError):
                profile.stage_overlay(overlay, root)
            (overlay / 'etc/link').unlink()
            (overlay / 'bin').mkdir()
            (overlay / 'bin/sh').write_text('replace')
            with self.assertRaises(ValueError):
                profile.stage_overlay(overlay, root)
            self.assertEqual((root / 'bin/sh').read_text(), 'base')

    def test_overlay_requires_directories_and_preserves_new_modes(self):
        import stat
        import tempfile
        with tempfile.TemporaryDirectory() as name:
            base = Path(name)
            root, overlay = base / 'root', base / 'overlay'
            root.mkdir(mode=0o755)
            overlay.mkdir()
            (root / 'etc').mkdir(mode=0o700)
            (overlay / 'etc').mkdir(mode=0o755)
            (overlay / 'etc/config').write_text('fixture')
            (overlay / 'private').mkdir(mode=0o700)
            (overlay / 'private/key').write_text('fixture')
            (overlay / 'private/key').chmod(0o600)
            profile.stage_overlay(overlay, root)
            self.assertEqual(stat.S_IMODE((root / 'private').stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE((root / 'private/key').stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE((root / 'etc').stat().st_mode), 0o700)
            with self.assertRaises(ValueError):
                profile.stage_overlay(base / 'missing', root)
            with self.assertRaises(ValueError):
                profile.stage_overlay(overlay, base / 'missing')
            (root / 'link').symlink_to(base, target_is_directory=True)
            (overlay / 'link').mkdir()
            with self.assertRaises(ValueError):
                profile.stage_overlay(overlay, root)

    def test_compact_console_uses_native_crunch_components(self):
        selected = profile.console('riscv32', compact=True)
        self.assertIn('sbin/ifconfig', selected['programs'])
        self.assertIn('sbin/reboot', selected['programs'])
        self.assertIn('bin/sh', selected['programs'])
        self.assertIn('sbin/init', selected['programs'])
        self.assertIn('lib/libprop', selected['libraries'])
        self.assertIn('lib/libkvm', selected['libraries'])
        self.assertIn('lib/libm', selected['libraries'])
        self.assertTrue(selected['compact'])

    def test_compact_reboot_dispatch_and_halt_path(self):
        import os
        import subprocess
        import tempfile
        shell = os.environ.get('TEST_SH')
        if not shell:
            self.fail('Set TEST_SH to the selected native shell')
        source = path.with_name('cix-p1-build-userland.sh').read_text()
        start = source.index("        {\n            printf 'srcdirs bin sbin")
        end = source.index('        } > "$out/crunch/console-tools.conf"', start)
        block = source[start:end] + '        } > "$out/crunch/console-tools.conf"\n'
        with tempfile.TemporaryDirectory() as name:
            out = Path(name)
            (out / 'crunch').mkdir()
            subprocess.run([shell, '-c', 'set -eu\n' + block], check=True,
                           env=dict(os.environ, out=str(out),
                                    programs='bin/sh sbin/reboot'))
            config = (out / 'crunch/console-tools.conf').read_text()
            self.assertIn('progs sh reboot', config)
            self.assertIn('ln reboot halt', config)

            start = source.index('    if [ "$profile" = console-tools ]; then\n'
                                 '        cp "$out/crunch/console-tools"')
            end = source.index('    if [ -n "$extra" ]; then', start)
            stage = source[start:end]
            (out / 'root/bin').mkdir(parents=True)
            (out / 'root/sbin').mkdir()
            (out / 'crunch/console-tools').write_bytes(b'crunch fixture')
            subprocess.run([shell, '-c', 'set -eu\n' + stage], check=True,
                           env=dict(os.environ, out=str(out), profile='console-tools',
                                    programs='bin/sh sbin/reboot'))
            self.assertEqual(os.readlink(out / 'root/sbin/reboot'),
                             '../bin/console-tools')
            self.assertEqual(os.readlink(out / 'root/sbin/halt'),
                             '../bin/console-tools')

    def test_compact_console_validates_crunch_and_llvm_tools(self):
        from types import SimpleNamespace
        seen = []
        native = SimpleNamespace(host={'os': 'Darwin', 'cpu': 'arm64'},
                                 bundle_root='/native')
        profile.check_os_tools(Path('/tools'), native, '/selected/bin',
                               lambda *args, **kw: seen.append(args[:2]), compact=True)
        self.assertIn(('/tools', '/tools/bin/nbcrunchgen'), seen)
        self.assertIn(('/native', '/native/bin/llvm-nm'), seen)
        self.assertIn(('/native', '/native/bin/llvm-objcopy'), seen)

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
