#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the legacy header and data CRCs for an evbarm mk.generic image."""

from pathlib import Path
import struct
import sys
import zlib


def check(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64:
            raise ValueError("incomplete uImage header")
        magic, header_crc, _, size, _, entry, data_crc = struct.unpack_from(">7I", header)
        if magic != 0x27051956 or zlib.crc32(header[:4] + bytes(4) + header[8:]) != header_crc:
            raise ValueError("invalid uImage header or checksum")
        # Linux, ARM, kernel_noload, uncompressed. Entry zero selects the first payload byte.
        if header[28:32] != bytes((5, 2, 14, 0)) or entry != 0:
            raise ValueError("expected an ARM kernel_noload image with entry offset zero")
        if not size or path.stat().st_size != 64 + size:
            raise ValueError("uImage size does not match its payload")
        checksum = 0
        while block := stream.read(1024 * 1024):
            checksum = zlib.crc32(block, checksum)
        if checksum != data_crc:
            raise ValueError("uImage payload checksum mismatch")


if __name__ == "__main__":
    try:
        check(Path(sys.argv[1]))
    except (IndexError, OSError, ValueError) as error:
        sys.exit(f"ARMv7 boot image: {error}")
