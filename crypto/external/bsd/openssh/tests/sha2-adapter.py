#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Run the OpenSSH SHA2 adapter against EmberBSD libc SHA2 under sanitizers."""
import argparse
import platform
import sys
from pathlib import Path
import subprocess
import tempfile


parser = argparse.ArgumentParser()
parser.add_argument('--clang', type=Path, required=True)
parser.add_argument('--cppcheck', type=Path, required=True)
parser.add_argument('--work-dir', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[5]
machine = platform.machine().lower()
if machine in ('arm64', 'aarch64', 'x86_64', 'amd64'):
    cppcheck_platform = 'unix64'
elif machine in ('i386', 'i686'):
    cppcheck_platform = 'unix32'
else:
    raise SystemExit('unsupported native host architecture for libc SHA2 test: ' + machine)

source = root / 'common/lib/libc/hash/sha2/sha2.c'
test = Path(__file__).with_suffix('.c')
args.work_dir.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='openssh-sha2-adapter-', dir=args.work_dir) as tmp:
    tmp = Path(tmp)
    include = tmp / 'include'
    include.mkdir()
    (include / 'sys').mkdir()
    # Expose only EmberBSD's public SHA2 declarations. Standard system headers
    # and libc startup/linkage remain native to the host under test.
    (include / 'sha2.h').symlink_to(root / 'sys/sys/sha2.h')
    (include / 'sys/sha2.h').symlink_to(root / 'sys/sys/sha2.h')
    if sys.platform == 'darwin':
        endian_header = '''/* Test-only endian interface backed by native Darwin APIs. */
#include <libkern/OSByteOrder.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t be32dec(const void *p) { uint32_t v; memcpy(&v, p, sizeof(v)); return OSSwapBigToHostInt32(v); }
static inline uint64_t be64dec(const void *p) { uint64_t v; memcpy(&v, p, sizeof(v)); return OSSwapBigToHostInt64(v); }
static inline void be32enc(void *p, uint32_t v) { v = OSSwapHostToBigInt32(v); memcpy(p, &v, sizeof(v)); }
static inline void be64enc(void *p, uint64_t v) { v = OSSwapHostToBigInt64(v); memcpy(p, &v, sizeof(v)); }
#define htobe64(v) OSSwapHostToBigInt64(v)
'''
    elif sys.platform.startswith('linux'):
        endian_header = '''/* Test-only endian interface backed by native Linux APIs. */
#include <endian.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t be32dec(const void *p) { uint32_t v; memcpy(&v, p, sizeof(v)); return be32toh(v); }
static inline uint64_t be64dec(const void *p) { uint64_t v; memcpy(&v, p, sizeof(v)); return be64toh(v); }
static inline void be32enc(void *p, uint32_t v) { v = htobe32(v); memcpy(p, &v, sizeof(v)); }
static inline void be64enc(void *p, uint64_t v) { v = htobe64(v); memcpy(p, &v, sizeof(v)); }
'''
    else:
        raise SystemExit('host SHA2 KAT supports Darwin and Linux endian APIs')
    (include / 'sys/endian.h').write_text(endian_header)
    (include / 'namespace.h').write_text(
        '/* Host-only ABI shim: test libc symbols directly without NetBSD aliases. */\n'
        '#undef __weak_alias\n')
    include_args = ['-I', str(include)]
    feature_args = ['-D_DEFAULT_SOURCE=1'] if sys.platform.startswith('linux') else []
    common = [str(args.clang), '-std=c11', '-Wall', '-Wextra', '-Werror',
              *feature_args, '-DHAVE_SYS_ENDIAN_H=1', '-include', 'stdint.h',
              *include_args]

    subprocess.run([str(args.cppcheck), '--clang=' + str(args.clang),
                    '--enable=warning,style,performance,portability',
                    '--error-exitcode=1', '--std=c11',
                    '--platform=' + cppcheck_platform,
                    *include_args, str(test)], check=True)
    subprocess.run([*common, '--analyze', '-o', str(tmp / 'sha2-adapter.plist'),
                    str(test)], check=True)

    binary = tmp / 'sha2-adapter'
    subprocess.run([*common, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(test), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
