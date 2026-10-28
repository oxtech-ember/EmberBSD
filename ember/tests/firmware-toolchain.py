#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise real SDK compiler selection for freestanding firmware."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

root = Path(__file__).resolve().parents[2]

class FirmwareToolchain(unittest.TestCase):
    def test_builder_requires_explicit_prefixes_before_build(self):
        with tempfile.TemporaryDirectory() as cache:
            env = {'PATH': cache, 'HOME': cache,
                   'EMBERBSD_SDK_SOURCE': os.environ['EMBERBSD_SDK_SOURCE']}
            result = subprocess.run([str(Path(os.environ['EMBER_HOST_PREFIX']) / 'bin/bash'), str(root / 'ember/build-firmware.sh'), cache],
                                    env=env, text=True, capture_output=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('EMBER_HOST_PREFIX', result.stderr)
            self.assertFalse((Path(cache) / 'firmware').exists())

    def test_prepare_missing_awk_refuses_before_lock(self):
        prefix = Path(os.environ['EMBER_HOST_PREFIX'])
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            tools = base / 'tools'
            tools.mkdir()
            for name in ('dirname', 'ls'):
                (tools / name).symlink_to(prefix / 'gnu/bin' / name)
            cache = base / 'cache'
            cache.mkdir()
            env = {'PATH': str(tools), 'BASH': str(prefix / 'bin/bash')}
            result = subprocess.run([str(prefix / 'bin/bash'), str(root / 'ember/build-firmware.sh'),
                                     str(cache), '--prepare'], env=env, text=True, capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing host tool: awk', result.stderr)
            self.assertFalse((cache / '.ember-firmware.lock').exists())

    def test_prepare_failing_awk_refuses_before_lock(self):
        prefix = Path(os.environ['EMBER_HOST_PREFIX'])
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            tools = base / 'tools'
            tools.mkdir()
            for name in ('dirname', 'ls'):
                (tools / name).symlink_to(prefix / 'gnu/bin' / name)
            awk = tools / 'awk'
            awk.write_text('#!' + str(prefix / 'bin/mksh') + '\nexit 77\n')
            awk.chmod(0o755)
            cache = base / 'cache'
            cache.mkdir()
            env = {'PATH': str(tools), 'BASH': str(prefix / 'bin/bash')}
            result = subprocess.run([str(prefix / 'bin/bash'), str(root / 'ember/build-firmware.sh'),
                                     str(cache), '--prepare'], env=env, text=True, capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unusable host tool: awk', result.stderr)
            self.assertFalse((cache / '.ember-firmware.lock').exists())

    def test_prepare_failed_lease_identity_releases_owned_empty_lock(self):
        prefix = Path(os.environ['EMBER_HOST_PREFIX'])
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            tools = base / 'tools'
            tools.mkdir()
            for name in ('dirname', 'ls', 'mkdir', 'rmdir'):
                (tools / name).symlink_to(prefix / 'gnu/bin' / name)
            counter = base / 'awk-called'
            awk = tools / 'awk'
            awk.write_text('#!' + str(prefix / 'bin/mksh') + '\n'
                           'if [ -e "$AWK_COUNTER" ]; then exit 77; fi\n'
                           ': > "$AWK_COUNTER"\nexit 0\n')
            awk.chmod(0o755)
            cache = base / 'cache'
            cache.mkdir()
            env = {'PATH': str(tools), 'BASH': str(prefix / 'bin/bash'), 'AWK_COUNTER': str(counter)}
            result = subprocess.run([str(prefix / 'bin/bash'), str(root / 'ember/build-firmware.sh'),
                                     str(cache), '--prepare'], env=env, text=True, capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('cannot identify firmware cache lease', result.stderr)
            self.assertFalse((cache / '.ember-firmware.lock').exists())

    def test_builder_missing_native_iasl_preserves_output(self):
        prefix = Path(os.environ['EMBER_HOST_PREFIX'])
        if (prefix / 'bin/iasl').exists():
            self.skipTest('selected prefix already has iasl')
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / 'cache'
            output = cache / 'firmware'
            output.mkdir(parents=True)
            image = output / 'RPI_EFI.fd'
            image.write_bytes(b'prior firmware')
            env = dict(os.environ, EMBERBSD_SDK_SOURCE=os.environ['EMBERBSD_SDK_SOURCE'],
                       EMBER_HOST_PREFIX=str(prefix), EMBER_TARGET_PREFIX=os.environ['EMBER_TARGET_PREFIX'])
            env['EMBER_FIRMWARE_INTERNAL'] = '1'  # An inherited value must not bypass selection.
            result = subprocess.run([str(prefix / 'bin/bash'), str(root / 'ember/build-firmware.sh'), str(cache)],
                                    env=env, text=True, capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing native tool in host prefix: iasl', result.stderr)
            self.assertEqual(image.read_bytes(), b'prior firmware')

    def test_gcc_validates_driver_tools_and_components(self):
        spec = importlib.util.spec_from_file_location("firmware_selector", root / "ember/tools/firmware-toolchain.py")
        selector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(selector)
        from types import SimpleNamespace
        prefix = "/selected"
        cross = prefix + "/bin/aarch64-elf-"
        selected = []
        def validate(root_path, path, host, *, search_path):
            selected.append(path)
            return path
        def capture(argv, *, env):
            name = argv[-1].split("=", 1)[1]
            path = cross + name if name in ("as", "ld") else prefix + "/libexec/" + name
            return SimpleNamespace(stdout=(path + "\n").encode())
        selector.validate_gcc_tools(prefix, cross, {}, "/host/bin", validate, capture)
        for name in ("gcc", "gcc-ar", "as", "ld", "objcopy", "objdump"):
            self.assertIn(cross + name, selected)
        for name in ("cc1", "collect2", "lto1", "lto-wrapper"):
            self.assertIn(prefix + "/libexec/" + name, selected)
        with self.assertRaises(ValueError):
            selector.validate_gcc_tools(prefix, "/external/bin/aarch64-elf-", {}, "/host/bin", validate, capture)

    def test_unsupported_host_refused(self):
        sys.path.insert(0, str(Path(os.environ['EMBERBSD_SDK_SOURCE']) / 'src'))
        from emberbsd_sdk import host_prefix
        from emberbsd_sdk.errors import SDKError
        with self.assertRaises(SDKError):
            host_prefix.load_host_prefix(os.environ['EMBER_HOST_PREFIX'],
                                         host={'os': 'Unsupported', 'cpu': 'arm64'},
                                         required_roles={'python'})

    def test_sdk_discards_ambient_search_paths(self):
        sys.path.insert(0, str(Path(os.environ['EMBERBSD_SDK_SOURCE']) / 'src'))
        from emberbsd_sdk import host_prefix, native_checks
        native = host_prefix.load_host_prefix(os.environ['EMBER_HOST_PREFIX'],
                    compiler_family='clang', required_roles={'cc', 'cxx', 'cpp', 'sh', 'python', 'awk'})
        poisoned = {key: '/unsupported/firmware-search' for key in
                    ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                     'COMPILER_PATH', 'GCC_EXEC_PREFIX', 'LD_LIBRARY_PATH', 'CFLAGS',
                     'CPPFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CC', 'CXX', 'CPP', 'MAKEFLAGS')}
        with tempfile.TemporaryDirectory() as work:
            selected = native_checks.check_environment(native, bindings=str(Path(work) / 'bindings'),
                                                        environment=poisoned)
            awk = Path(work) / 'bindings/awk'
            self.assertEqual(awk.resolve(), Path(native.tool('awk')).resolve())
            subprocess.run([str(awk), 'BEGIN { exit 0 }'], env=selected, check=True)
        for key, value in poisoned.items():
            self.assertNotEqual(selected.get(key), value, key)

    def test_missing_elftools_fails_preflight(self):
        # Exercise the selected host Python check even when another required
        # native role (such as iasl) is absent on this machine.
        sys.path.insert(0, str(Path(os.environ['EMBERBSD_SDK_SOURCE']) / 'src'))
        from emberbsd_sdk import host_prefix
        spec = importlib.util.spec_from_file_location('firmware_selector', root / 'ember/tools/firmware-toolchain.py')
        selector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(selector)
        native = mock.Mock()
        native.path = str(Path(sys.executable).parent)
        native.host = {}
        native.tool.return_value = sys.executable
        argv = ['firmware-toolchain.py', '--sdk-source', os.environ['EMBERBSD_SDK_SOURCE'],
                '--host-prefix', os.environ['EMBER_HOST_PREFIX'], '--target-prefix', os.environ['EMBER_TARGET_PREFIX'],
                '--target-family', 'gcc', '--run', '--', '/does/not/run']
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(host_prefix, 'load_host_prefix', return_value=native), \
             mock.patch.object(selector, 'validate_gcc_tools'), \
             mock.patch.object(selector.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'python')), \
             contextlib.redirect_stderr(io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as exit_status:
                selector.main()
        self.assertEqual(exit_status.exception.code, 1)
        self.assertIn('selected native Python is missing pyelftools', errors.getvalue())

    def test_verified_codecs_preserve_builder_command(self):
        sys.path.insert(0, str(Path(os.environ["EMBERBSD_SDK_SOURCE"]) / "src"))
        from emberbsd_sdk import host_prefix, native_checks, process, stream
        spec = importlib.util.spec_from_file_location("firmware_selector", root / "ember/tools/firmware-toolchain.py")
        selector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(selector)
        native = mock.Mock()
        native.path, native.host = str(Path(sys.executable).parent), {}
        native.tool.side_effect = lambda role: sys.executable if role == "python" else "/selected/" + role
        argv = ["firmware-toolchain.py", "--sdk-source", os.environ["EMBERBSD_SDK_SOURCE"],
                "--host-prefix", os.environ["EMBER_HOST_PREFIX"], "--target-prefix", os.environ["EMBER_TARGET_PREFIX"],
                "--target-family", "gcc", "--run", "--", "/selected/builder", "argument"]
        def check_environment(_native, *, bindings):
            Path(bindings).mkdir()
            return {"CC": "cc", "CPP": "cpp", "PATH": bindings}
        with tempfile.TemporaryDirectory() as directory:
            values = {name: directory for name in ("EMBER_PORTS_SOURCE", "EMBER_7ZIP_SOURCE", "EMBER_BROTLI_SOURCE")}
            values["EMBER_GIT"] = "/usr/bin/git"
            with mock.patch.dict(os.environ, values), mock.patch.object(sys, "argv", argv), \
                 mock.patch.object(host_prefix, "load_host_prefix", return_value=native), \
                 mock.patch.object(selector, "validate_gcc_tools"), \
                 mock.patch.object(process, "capture") as verify, \
                 mock.patch.object(native_checks, "check_environment", side_effect=check_environment), \
                 mock.patch.object(stream, "run_streamed") as run:
                run.return_value.stderr = b""
                verify.return_value.stdout = json.dumps({"tree_sha256": "a" * 64}).encode()
                self.assertEqual(selector.main(), 0)
                self.assertEqual(verify.call_count, 2)
                self.assertEqual(run.call_args.args[0], ["/selected/bash", "/selected/builder", "argument"])

    def test_missing_selected_host_tool_refuses_ambient_path(self):
        command = [sys.executable, '-B', str(root / 'ember/tools/firmware-toolchain.py'),
                   '--sdk-source', os.environ['EMBERBSD_SDK_SOURCE'],
                   '--host-prefix', os.environ['EMBER_HOST_PREFIX'],
                   '--target-prefix', os.environ['EMBER_TARGET_PREFIX'],
                   '--host-role', 'ember-firmware-test-missing']
        with tempfile.TemporaryDirectory() as ambient:
            tool = Path(ambient) / 'ember-firmware-test-missing'
            tool.write_text('#!/bin/sh\nexit 0\n')
            tool.chmod(0o755)
            env = dict(os.environ, PATH=ambient + os.pathsep + os.environ['PATH'])
            result = subprocess.run(command, text=True, capture_output=True, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing native tool in host prefix: ember-firmware-test-missing', result.stderr)

    def test_real_clang_freestanding_lto_does_not_search_host_sdk(self):
        prefix = Path(os.environ['EMBER_TARGET_PREFIX'])
        clang, lld = prefix / 'bin/clang', prefix / 'bin/ld.lld'
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            source = work / 'test.c'
            source.write_text('void entry(void) {}\n')
            flags = ['--target=aarch64-unknown-none-elf', '--sysroot=' + str(work),
                     '--ld-path=' + str(lld), '-ffreestanding', '-nostdlib', '-flto',
                     '-Wl,-e,entry']
            env = dict(os.environ, PATH=str(prefix / 'bin'))
            expanded = subprocess.run([str(clang), *flags, '-###', str(source), '-o', str(work / 'test.elf')],
                                      text=True, capture_output=True, env=env, check=True).stderr
            self.assertNotIn('MacOSX', expanded)
            self.assertIn(str(lld), expanded)
            macros = subprocess.run([str(clang), *flags[:3], '-dM', '-E', '-x', 'c', '/dev/null'],
                                    text=True, capture_output=True, env=env, check=True).stdout
            self.assertIn('#define __aarch64__ 1', macros)
            self.assertNotIn('#define __linux__', macros)
            self.assertNotIn('#define __NetBSD__', macros)
            subprocess.run([str(clang), *flags, str(source), '-o', str(work / 'test.elf')],
                           env=env, check=True, capture_output=True)
            result = subprocess.run([str(prefix / 'bin/llvm-readelf'), '-h', str(work / 'test.elf')],
                                    text=True, capture_output=True, env=env, check=True).stdout
            self.assertIn('AArch64', result)

    def test_selected_clang_is_freestanding_and_separate(self):
        command = [sys.executable, '-B', str(root / 'ember/tools/firmware-toolchain.py'),
                   '--sdk-source', os.environ['EMBERBSD_SDK_SOURCE'],
                   '--host-prefix', os.environ['EMBER_HOST_PREFIX'],
                   '--target-prefix', os.environ['EMBER_TARGET_PREFIX']]
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        selection = json.loads(result.stdout)
        self.assertEqual(selection['target']['triplet'], 'aarch64-unknown-none-elf')
        self.assertEqual(selection['host']['compiler']['family'], 'clang')
        self.assertEqual(selection['target']['family'], 'clang')
        self.assertIn('--target=aarch64-unknown-none-elf', selection['target']['commands']['cc'])

if __name__ == '__main__':
    unittest.main()
