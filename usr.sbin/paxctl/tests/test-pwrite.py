#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. Origin: EmberBSD (AI-assisted).
"""Exercise the actual paxctl CLI with controlled pwrite outcomes."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

source = Path(os.environ.get('PAXCTL_SOURCE', Path(__file__).resolve().parent.parent / 'paxctl.c')).resolve()
cc = os.environ['CC']
with tempfile.TemporaryDirectory(prefix='paxctl-pwrite-') as directory:
    root = Path(directory)
    (root / 'nbinclude/machine').mkdir(parents=True)
    (root / 'nbinclude/machine/elf_machdep.h').write_text('')
    (root / 'nbtool_config.h').write_text('''#include <stdint.h>\n#include <sys/cdefs.h>\n#undef __dead\n#define __dead __attribute__((noreturn))\n#define bswap16 __builtin_bswap16\n#define bswap32 __builtin_bswap32\n#define bswap64 __builtin_bswap64\n''')
    (root / 'inject.c').write_text('''#include <errno.h>\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\nssize_t test_pwrite(int fd, const void *p, size_t n, off_t off) {\n const char *mode = getenv("PAXCTL_TEST_WRITE");\n if (mode && strcmp(mode, "error") == 0) { errno = EIO; return -1; }\n if (mode && strcmp(mode, "short") == 0) return pwrite(fd, p, n - 1, off);\n return pwrite(fd, p, n, off);\n}\n''')
    exe = root / 'paxctl'
    obj = root / 'paxctl.o'
    subprocess.run([cc, '-DHAVE_NBTOOL_CONFIG_H=1', '-Dpwrite=test_pwrite', '-I'+str(root), '-c', str(source), '-o', str(obj)], check=True)
    subprocess.run([cc, str(obj), str(root / 'inject.c'), '-o', str(exe)], check=True)
    for bits in (32, 64):
        for endian, data in (("<", 1), (">", 2)):
            ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, data, 1]) + bytes(9)
            if bits == 32:
                ehsize, shsize = 52, 40
                hdr = struct.pack(endian + "16sHHIIIIIHHHHHH", ident, 2, 3, 1, 0, 0, ehsize, 0, ehsize, 0, 0, shsize, 1, 0)
                section = struct.pack(endian + "IIIIIIIIII", 0, 7, 0, 0, ehsize + shsize, 20, 0, 0, 4, 0)
            else:
                ehsize, shsize = 64, 64
                hdr = struct.pack(endian + "16sHHIQQQIHHHHHH", ident, 2, 62, 1, 0, 0, ehsize, 0, ehsize, 0, 0, shsize, 1, 0)
                section = struct.pack(endian + "IIQQQQIIQQ", 0, 7, 0, 0, ehsize + shsize, 20, 0, 0, 4, 0)
            fixture = hdr + section + struct.pack(endian + "III4sI", 4, 4, 3, b"PaX\0", 0)
            for mode in os.environ.get("PAXCTL_TEST_CASES", "error,short,success").split(","):
                target = root / (str(bits) + str(data) + mode)
                target.write_bytes(fixture)
                env = dict(os.environ, PAXCTL_TEST_WRITE=mode)
                result = subprocess.run([str(exe), "+m", str(target)], env=env, capture_output=True)
                if mode == "success":
                    assert result.returncode == 0, result.stderr
                    assert target.read_bytes() != fixture
                    listing = subprocess.run([str(exe), str(target)], capture_output=True, check=True)
                    assert b"m:" in listing.stdout, listing.stdout
                else:
                    assert result.returncode != 0, (bits, endian, mode, result.stderr)
                    assert (b"Can't modify flags" if mode == "error" else b"Short write") in result.stderr, result.stderr
                    if mode == "error":
                        assert target.read_bytes() == fixture
    print('paxctl pwrite: error, short write and successful update PASS')
