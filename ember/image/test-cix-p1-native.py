#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check SDK selection and refusal without starting a target build."""
if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import types

path = Path(__file__).with_name('cix-p1-native.py')
spec = importlib.util.spec_from_file_location('cix_native', path)
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)

class SDKError(Exception):
    pass

calls = []
class Native:
    bundle_root = '/native'
    host = {'os': 'Darwin', 'cpu': 'arm64'}
    def tool(self, role):
        return '/native/bin/python3.15'

native = types.SimpleNamespace(NATIVE_OS_API=1,
    select_native_os_tools=lambda prefix, source, compiler_family: (calls.append(('native', prefix, compiler_family)) or Native()),
    native_os_environment=lambda native, routing: {'PATH': '/native/bin'},
    run_native_os=lambda native, **kwargs: calls.append(('run', kwargs)))
app = types.SimpleNamespace(APPLICATION_TOOLCHAIN_API=2,
    load_clang_runtime=lambda root, target: (calls.append(('runtime', root, target)) or
        {'artifacts': {'builtins': 'usr/pkg/llvm/builtins.a'},
         'source': {'archive': 'llvm.tar.xz', 'sha512': 'abc'}}),
    clang_application_link_flags=lambda compiler, root, record: calls.append(('validate', compiler, root)))
llvm = types.SimpleNamespace(runtime_paths=lambda root, record: {'runtime-prefix': root + '/usr/pkg/llvm',
    'llvm-resource': root + '/usr/pkg/llvm/lib/clang/23', 'llvm-unwind': root + '/usr/pkg/llvm/lib/libunwind.a'})
target = types.SimpleNamespace(clang_compiler=lambda prefix, triplet, **kwargs: (calls.append(('clang', prefix, triplet)) or
    {'resource_dir': prefix + '/lib/clang/23', 'version': '23.1.4', 'reported_triplet': 'aarch64-unknown-netbsd'}))
class Extra:
    def tool(self, role):
        return '/selected/' + role
host = types.SimpleNamespace(load_host_prefix=lambda *args, **kwargs:
    (calls.append(('host-extra', kwargs)) or Extra()))
jsonio = types.SimpleNamespace(loads_json=lambda data: {'format': 'emberbsd-llvm-source',
    'version': 1, 'llvm_version': '23.1.4', 'source': {'archive': 'llvm.tar.xz', 'sha512': 'abc'}})
libedit = types.SimpleNamespace(validate_source=lambda source, ports, git: calls.append(('libedit', source, ports, git)))
process = types.SimpleNamespace(capture=lambda argv, env: (calls.append(('verify-source', argv)) or
    types.SimpleNamespace(stdout=b'{}')))
pkg = types.ModuleType('emberbsd_sdk')
for name, obj in [('native_os', native), ('application_toolchain', app), ('llvm_runtime', llvm),
                  ('target_toolchain', target), ('host_prefix', host), ('jsonio', jsonio), ('libedit', libedit), ('process', process)]:
    setattr(pkg, name, obj)
sys.modules['emberbsd_sdk'] = pkg
errors = types.ModuleType('emberbsd_sdk.errors')
errors.SDKError = SDKError
sys.modules['emberbsd_sdk.errors'] = errors

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'source'
    source.mkdir()
    (source / 'build.sh').touch()
    libedit_source = Path(tmp) / 'libedit'
    libedit_source.mkdir()
    (libedit_source / 'libedit-source.json').touch()
    (libedit_source / 'ports.distinfo').touch()
    tools = Path(tmp) / 'tools'
    (tools / 'bin').mkdir(parents=True)
    # A native-mode TOOLDIR need not contain nbmake; make comes from the routing.
    for name in ('nbmkesdb', 'nbmkcsmapper'):
        p = tools / 'bin' / name
        p.touch(mode=0o755)
    bfd = Path(tmp) / 'host-bfd.mk'
    bfd.touch()
    llvm_source = Path(tmp) / 'llvm'
    (llvm_source / 'runtimes').mkdir(parents=True)
    (llvm_source / 'runtimes/CMakeLists.txt').touch()
    os.environ.update(EMBERBSD_SDK_SOURCE='/sdk', EMBER_HOST_PREFIX='/native',
                      EMBER_USERLAND_PROFILE='cix',
                      EMBER_LIBEDIT_SOURCE=str(libedit_source), EMBER_HOST_BFD_CONFIG=str(bfd),
                      EMBER_SDK_SYSROOT='/sysroot', EMBER_LLVM_SOURCE=str(llvm_source),
                      EMBER_PORTS_SOURCE='/ports', EMBER_GIT='/usr/bin/git',
                      EMBER_LIBARCHIVE_SOURCE='/libarchive', EMBER_LIBEVENT_SOURCE='/libevent',
                      EMBER_BYACC_SOURCE='/byacc', EMBER_ISCSI_SOURCE='/iscsi', EMBER_LIBPCAP_SOURCE='/libpcap',
                      EMBER_TCP_WRAPPERS_SOURCE='/tcpwrappers')
    sys.argv = [str(path), str(source), tmp + '/clang', str(tools), '/compiler']
    subject.main()
    run = next(row[1] for row in calls if row[0] == 'run')
    assert run['shell'] and run['source'] == str(source)
    assert ('runtime', '/sysroot', 'aarch64') in calls
    assert any(row[0] == 'verify-source' and 'verify-source' in row[1] for row in calls)
    assert any(row[0] == 'verify-source' and '/ports/scripts/libarchive-source.py' in row[1] for row in calls)
    assert any(row[0] == 'verify-source' and '/ports/scripts/source-inputs/libevent/source.py' in row[1] for row in calls)
    assert ('libedit', str(libedit_source), '/ports', '/usr/bin/git') in calls
    assert ('clang', '/compiler', 'aarch64--netbsd') in calls
    assert run['arguments'][1:10] == ['--selected', str(source), tmp + '/clang', str(tools),
                                       '/compiler', '/sdk', '/native', 'clang', 'clang']
    assert '/sysroot/usr/pkg/llvm/builtins.a' in run['arguments']
    assert ['/usr/bin/git', '/native/bin/python3.15', '/selected/shasum'] == run['arguments'][14:17]
    assert run['arguments'][17:23] == ['/libarchive', '/libevent', '/byacc', '/iscsi', '/libpcap', '/tcpwrappers']
    assert any(row[0] == 'verify-source' and
               '/ports/scripts/source-inputs/byacc/source.py' in row[1] and
               row[1][-2:] == ['--source', '/byacc'] for row in calls)
    assert any(row[0] == 'verify-source' and
               '/ports/scripts/source-inputs/iscsi/source.py' in row[1] and
               row[1][-2:] == ['--source', '/iscsi'] for row in calls)
    assert any(row[0] == 'verify-source' and
               '/ports/scripts/source-inputs/libpcap/source.py' in row[1] and
               row[1][-2:] == ['--source', '/libpcap'] for row in calls)
    assert any(row[0] == 'verify-source' and
               '/ports/scripts/source-inputs/tcp_wrappers/source.py' in row[1] and
               row[1][-2:] == ['--source', '/tcpwrappers'] for row in calls)
    assert ('host-extra', {'required_roles': {'shasum'}, 'compiler_family': 'clang'}) in calls
    calls.clear()
    del os.environ['EMBER_BYACC_SOURCE']
    sys.argv[2] = tmp + '/missing-byacc'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'EMBER_BYACC_SOURCE' in str(exc)
    else:
        raise AssertionError('missing byacc source accepted')
    assert not Path(sys.argv[2]).exists()
    os.environ['EMBER_BYACC_SOURCE'] = '/byacc'
    del os.environ['EMBER_ISCSI_SOURCE']
    sys.argv[2] = tmp + '/missing-iscsi'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'EMBER_ISCSI_SOURCE' in str(exc)
    else:
        raise AssertionError('missing iSCSI source accepted')
    assert not Path(sys.argv[2]).exists()
    os.environ['EMBER_ISCSI_SOURCE'] = '/iscsi'
    del os.environ['EMBER_LIBPCAP_SOURCE']
    sys.argv[2] = tmp + '/missing-libpcap'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'EMBER_LIBPCAP_SOURCE' in str(exc)
    else:
        raise AssertionError('missing libpcap source accepted')
    assert not Path(sys.argv[2]).exists()
    os.environ['EMBER_LIBPCAP_SOURCE'] = '/libpcap'
    del os.environ['EMBER_TCP_WRAPPERS_SOURCE']
    sys.argv[2] = tmp + '/missing-tcpwrappers'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'EMBER_TCP_WRAPPERS_SOURCE' in str(exc)
    else:
        raise AssertionError('missing TCP wrappers source accepted')
    assert not Path(sys.argv[2]).exists()
    os.environ['EMBER_TCP_WRAPPERS_SOURCE'] = '/tcpwrappers'
    calls.clear()
    original = jsonio.loads_json
    jsonio.loads_json = lambda data: {'format': 'emberbsd-llvm-source', 'version': 1,
        'llvm_version': '23.1.4', 'source': {'archive': 'other.tar.xz', 'sha512': 'abc'}}
    sys.argv[2] = tmp + '/mismatch'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'differs' in str(exc)
    else:
        raise AssertionError('mismatched LLVM source accepted')
    assert not Path(sys.argv[2]).exists()
    jsonio.loads_json = original
    calls.clear()
    original_capture = process.capture
    def fail_libevent(argv, env):
        if '/ports/scripts/source-inputs/libevent/source.py' in argv:
            raise SDKError('libevent verification failed')
        return original_capture(argv, env)
    process.capture = fail_libevent
    sys.argv[2] = tmp + '/failed-source'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'libevent verification failed' in str(exc)
    else:
        raise AssertionError('failed source verification accepted')
    assert not Path(sys.argv[2]).exists()
    process.capture = original_capture
    calls.clear()
    def fail_byacc(argv, env):
        if '/ports/scripts/source-inputs/byacc/source.py' in argv:
            raise SDKError('byacc verification failed')
        return original_capture(argv, env)
    process.capture = fail_byacc
    sys.argv[2] = tmp + '/failed-byacc'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'byacc verification failed' in str(exc)
    else:
        raise AssertionError('failed byacc verification accepted')
    assert not Path(sys.argv[2]).exists()
    process.capture = original_capture
    calls.clear()
    def fail_iscsi(argv, env):
        if '/ports/scripts/source-inputs/iscsi/source.py' in argv:
            raise SDKError('iSCSI verification failed')
        return original_capture(argv, env)
    process.capture = fail_iscsi
    sys.argv[2] = tmp + '/failed-iscsi'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'iSCSI verification failed' in str(exc)
    else:
        raise AssertionError('failed iSCSI verification accepted')
    assert not Path(sys.argv[2]).exists()
    process.capture = original_capture
    calls.clear()
    def fail_libpcap(argv, env):
        if '/ports/scripts/source-inputs/libpcap/source.py' in argv:
            raise SDKError('libpcap verification failed')
        return original_capture(argv, env)
    process.capture = fail_libpcap
    sys.argv[2] = tmp + '/failed-libpcap'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'libpcap verification failed' in str(exc)
    else:
        raise AssertionError('failed libpcap verification accepted')
    assert not Path(sys.argv[2]).exists()
    process.capture = original_capture
    calls.clear()
    def fail_tcpwrappers(argv, env):
        if '/ports/scripts/source-inputs/tcp_wrappers/source.py' in argv:
            raise SDKError('TCP wrappers verification failed')
        return original_capture(argv, env)
    process.capture = fail_tcpwrappers
    sys.argv[2] = tmp + '/failed-tcpwrappers'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'TCP wrappers verification failed' in str(exc)
    else:
        raise AssertionError('failed TCP wrappers verification accepted')
    assert not Path(sys.argv[2]).exists()
    process.capture = original_capture
    calls.clear()
    os.environ.update(EMBER_TARGET_COMPILER_FAMILY='gcc', EMBER_HOST_COMPILER_FAMILY='gcc')
    del os.environ['EMBER_SDK_SYSROOT']
    sys.argv[2] = tmp + '/gcc'
    subject.main()
    assert ('native', '/native', 'gcc') in calls
    assert ('host-extra', {'required_roles': {'shasum'}, 'compiler_family': 'gcc'}) in calls
    assert not any(row[0] in ('runtime', 'clang') for row in calls)
    assert next(row[1] for row in calls if row[0] == 'run')['arguments'][9] == 'gcc'
    os.environ['EMBER_TARGET_COMPILER_FAMILY'] = 'clang'
    sys.argv[2] = tmp + '/missing'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'EMBER_SDK_SYSROOT' in str(exc)
    else:
        raise AssertionError('missing LLVM inputs accepted')
    assert not Path(sys.argv[2]).exists()
    os.environ['EMBER_SDK_SYSROOT'] = '/sysroot'
    (tools / 'bin' / 'nbmkesdb').unlink()
    sys.argv[2] = tmp + '/no-esdb'
    try:
        subject.main()
    except SystemExit as exc:
        assert 'missing selected OS host tool: nbmkesdb' in str(exc)
    else:
        raise AssertionError('missing nbmkesdb accepted')
    assert not Path(sys.argv[2]).exists()
print('PASS: selected clang, explicit GCC, missing inputs, native-mode TOOLDIR')
