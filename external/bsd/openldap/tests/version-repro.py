#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted OpenLDAP version metadata regression.
"""Compile real base version objects with reproducible and retained policy inputs."""
import argparse
import datetime
import os
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
for name in ('nbmake', 'source', 'dest', 'work-dir'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
producer = root / 'external/bsd/openldap/bin/ldapsearch'
epoch = 1794749420
env = dict(os.environ, USER='private-version-test-user')
env.pop('SOURCE_DATE_EPOCH', None)

def run(command):
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result.stdout

def expected_date(stamp):
    date = datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc)
    return f'{date:%b} {date.day:2d} {date:%Y %H:%M:%S}'.encode()

with tempfile.TemporaryDirectory(prefix='openldap-version-', dir=a.work_dir) as temporary:
    work = Path(temporary).resolve()
    objects = []
    commands = []
    for index in range(2):
        obj = work / ('host-' + str(index)) / ('deep-' * (index + 1))
        obj.mkdir(parents=True)
        command = [str(a.nbmake.resolve(strict=True)), '-j4', '-m', str(root / 'share/mk'),
                   '-C', str(producer), 'MAKEOBJDIR=' + str(obj), 'NETBSDOBJDIR=' + str(obj),
                   'NETBSDSRCDIR=' + str(root), 'DESTDIR=' + str(a.dest.resolve(strict=True)),
                   'EMBER_OPENLDAP_SOURCE=' + str(a.source.resolve(strict=True)), 'MKREPRO=yes',
                   'MKREPRO_TIMESTAMP=' + str(epoch)]
        if run(command + ['-V', '.OBJDIR']).strip() != str(obj):
            raise SystemExit('selected wrapper did not use the private object directory')
        run(command + ['version.o'])
        source = (obj / 'version.c').read_bytes()
        compiled = (obj / 'version.o').read_bytes()
        if b'private-version-test-user' in source or str(obj).encode() in source:
            raise SystemExit('OpenLDAP generated version leaks private build identity')
        if b'\\topenldap\\n' not in source or expected_date(epoch) not in compiled:
            raise SystemExit('OpenLDAP generated/compiled version does not use the selected reproducible epoch')
        if b'ldapsearch 2.' not in compiled or b'/Users/' in compiled:
            raise SystemExit('OpenLDAP version lost its application/version or retained a host path')
        before = [(obj / name).stat().st_mtime_ns for name in ('version.c', 'version.o')]
        run(command + ['version.o'])
        if before != [(obj / name).stat().st_mtime_ns for name in ('version.c', 'version.o')]:
            raise SystemExit('unchanged version policy rebuilt the version object')
        objects.append(compiled)
        commands.append((command, obj))
    if objects[0] != objects[1]:
        raise SystemExit('OpenLDAP version objects differ between host object roots')

    command, obj = commands[0]
    policy = Path(run(command + ['-V', '${LDAP_VERSION_POLICY}']).strip())
    # Pin old outputs/policy to one second ahead of wall clock. A rapid policy
    # update must work even when its filesystem timestamp cannot advance them.
    pinned = (int(time.time()) + 60) * 1_000_000_000
    for path in (obj / 'version.c', obj / 'version.o', policy):
        os.utime(path, ns=(pinned, pinned))
    changed = [word if not word.startswith('MKREPRO_TIMESTAMP=') else
               'MKREPRO_TIMESTAMP=' + str(epoch + 86400) for word in command]
    run(changed + ['version.o'])
    compiled = (obj / 'version.o').read_bytes()
    if compiled == objects[0] or expected_date(epoch + 86400) not in compiled:
        raise SystemExit('retained version object ignored a changed reproducible epoch')
    before = (obj / 'version.o').stat().st_mtime_ns
    run(changed + ['version.o'])
    if before != (obj / 'version.o').stat().st_mtime_ns:
        raise SystemExit('unchanged updated epoch rebuilt the version object')

    # A historical object has version.c/version.o but no policy record.
    command, obj = commands[1]
    policy = Path(run(command + ['-V', '${LDAP_VERSION_POLICY}']).strip())
    historical = [word if word != 'MKREPRO=yes' else 'MKREPRO=no' for word in command]
    (obj / 'version.c').unlink()
    (obj / 'version.o').unlink()
    run(historical + ['version.o'])
    if b'private-version-test-user' not in (obj / 'version.c').read_bytes():
        raise SystemExit('non-reproducible generation no longer preserves upstream metadata')
    if policy.is_file():
        policy.unlink()
    for name in ('version.c', 'version.o'):
        os.utime(obj / name, ns=(pinned, pinned))
    readonly = {name: (obj / name).stat().st_mtime_ns for name in ('version.c', 'version.o')}
    run(command + ['-V', '${LDAP_VERSION_POLICY}'])
    if readonly != {name: (obj / name).stat().st_mtime_ns for name in readonly}:
        raise SystemExit('metadata query invalidated version outputs')
    run(command + ['version.o'])
    if b'private-version-test-user' in (obj / 'version.c').read_bytes() or expected_date(epoch) not in (obj / 'version.o').read_bytes():
        raise SystemExit('retained historical version object was not refreshed')
print('PASS: real OpenLDAP version generation/compile, two roots, retained policy/epoch refresh and no-op')
