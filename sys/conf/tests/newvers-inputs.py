#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: AI-assisted.
"""Exercise newvers.mk's incremental version metadata dependencies."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import time


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host-prefix', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
args = parser.parse_args()

source = Path(__file__).resolve().parents[2]
conf = source / 'conf'
for path in (args.host_prefix, args.sysroot, conf / 'newvers.mk',
             conf / 'newvers.sh', conf / 'copyright'):
    if not path.exists():
        parser.error(f'missing required input: {path}')

bmake = args.host_prefix / 'bin/bmake'
shell = args.host_prefix / 'bin/mksh'
clang = args.host_prefix / 'bin/clang'
awk = args.host_prefix / 'bin/nawk'
date_tool = args.host_prefix / 'gnu/bin/date'
for path in (bmake, shell, clang, awk, date_tool):
    if not path.is_file():
        parser.error(f'missing required tool: {path}')

work = args.work_dir.resolve()
work.mkdir(parents=True, exist_ok=True)
build = work / 'newvers-inputs'
build.mkdir(exist_ok=True)
makefile = build / 'Makefile'
date = build / 'date-netbsd-compat'
date.write_text(f'''#!{shell}
if [ "$1" = -r ] && [ "$#" -eq 2 ]; then
    exec {date_tool} -u -d "@$2"
fi
exec {date_tool} "$@"
''')
date.chmod(0o755)
makefile.write_text(f'''S= {source}
HOST_SH= {shell}
TOOL_AWK= {awk}
TOOL_DATE= {date}
CC= {clang}
CFLAGS= --target=aarch64-unknown-netbsd --sysroot={args.sysroot}
CPPFLAGS=
SYSTEM_OBJ= system-sentinel.o
_MKMSG_CREATE= @:
_MKTARGET_COMPILE= @:
COMPILE_CTFCONVERT=
MACHINE= aarch64
KERNEL_BUILD= newvers-inputs-test
MKREPRO= yes
.export BUILDINFO

all: vers.o

.include "${{S}}/conf/newvers.mk"
''')

sentinel = build / 'system-sentinel.o'
sentinel.write_bytes(b'unrelated kernel object prerequisite\n')
sentinel_mtime = sentinel.stat().st_mtime_ns


def build_version(buildinfo, timestamp):
    result = subprocess.run([
        str(bmake), '-f', str(makefile), 'all',
        f'BUILDINFO={buildinfo}', f'MKREPRO_TIMESTAMP={timestamp}',
    ], cwd=build, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        raise RuntimeError(f'bmake failed ({result.returncode}):\n{result.stdout}')


def object_elf(path):
    data = path.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise AssertionError('vers.o is not a little-endian ELF64 object')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine != 183:
        raise AssertionError(f'vers.o has ELF machine {machine}, expected AArch64')
    return data


def put_targets_in_future(*paths):
    # Model a coarse-mtime filesystem: ordinary prerequisites appear newer
    # than this test's wall clock, so metadata changes must force vers.o.
    future_ns = time.time_ns() + 120_000_000_000
    for path in paths:
        os.utime(path, ns=(future_ns, future_ns))
        if path.stat().st_mtime_ns != future_ns:
            raise AssertionError(f'could not set future mtime on {path}')
    return future_ns


build_version('BUILDINFO-A-000000000000000000000000000000000000', 1700000000)
vers_o = build / 'vers.o'
stamp = build / 'vers.inputs'
initial = object_elf(vers_o)
initial_object_mtime = vers_o.stat().st_mtime_ns
initial_stamp_mtime = stamp.stat().st_mtime_ns
if b'BUILDINFO-A-000000000000000000000000000000000000' not in initial:
    raise AssertionError('initial BUILDINFO is absent from compiled vers.o')

# Repeating an unchanged profile must preserve both the stamp and object.
build_version('BUILDINFO-A-000000000000000000000000000000000000', 1700000000)
if vers_o.stat().st_mtime_ns != initial_object_mtime:
    raise AssertionError('unchanged profile rebuilt vers.o')
if stamp.stat().st_mtime_ns != initial_stamp_mtime:
    raise AssertionError('unchanged profile replaced vers.inputs')

# A build identity change updates the object while retaining unrelated inputs.
future_ns = put_targets_in_future(stamp, vers_o)
build_version('BUILDINFO-B-111111111111111111111111111111111111', 1700000000)
buildinfo_object = object_elf(vers_o)
if vers_o.stat().st_mtime_ns >= future_ns or buildinfo_object == initial:
    raise AssertionError('BUILDINFO change did not rebuild vers.o')
if b'BUILDINFO-B-111111111111111111111111111111111111' not in buildinfo_object:
    raise AssertionError('updated BUILDINFO is absent from compiled vers.o')
if b'BUILDINFO-A-000000000000000000000000000000000000' in buildinfo_object:
    raise AssertionError('stale BUILDINFO remains in compiled vers.o')
if stamp.stat().st_mtime_ns == initial_stamp_mtime:
    raise AssertionError('BUILDINFO change did not update vers.inputs')
if stamp.stat().st_mtime_ns >= future_ns:
    raise AssertionError('BUILDINFO change did not replace future-dated vers.inputs')

# The reproducible timestamp is part of _NVFLAGS and must regenerate version.
future_ns = put_targets_in_future(stamp, vers_o)
build_version('BUILDINFO-B-111111111111111111111111111111111111', 1700000100)
timestamp_object = object_elf(vers_o)
if vers_o.stat().st_mtime_ns >= future_ns or timestamp_object == buildinfo_object:
    raise AssertionError('timestamp change did not rebuild vers.o')
if stamp.stat().st_mtime_ns >= future_ns:
    raise AssertionError('timestamp change did not replace future-dated vers.inputs')
vers_c = (build / 'vers.c').read_text()
if 'Tue Nov 14 22:15:00 UTC 2023' not in vers_c:
    raise AssertionError('updated reproducible timestamp is absent from vers.c')

if sentinel.stat().st_mtime_ns != sentinel_mtime:
    raise AssertionError('test changed unrelated SYSTEM_OBJ sentinel mtime')
print('PASS: unchanged inputs preserve vers.o; BUILDINFO and timestamp changes rebuild AArch64 vers.o; SYSTEM_OBJ stays untouched')
