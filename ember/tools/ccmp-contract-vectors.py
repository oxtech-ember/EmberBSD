#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Regenerate the independent CCMP management fixtures (not a test dependency).

Uses an external OpenSSL/LibreSSL AES-ECB primitive and RFC 3610 CCM with
M=8, L=2. No EmberBSD crypto/header helper is used. Pass the selected SDK's
openssl executable as the only argument. Original, AI-assisted test utility.
"""
import subprocess
import sys

KEY = bytes(range(16))
NONCE = bytes.fromhex("10022233445566010203040506")
ADDRESSES = bytes.fromhex("021122334455022233445566023344556677")


def aes(block):
    assert len(block) == 16
    return subprocess.run(
        [sys.argv[1], "enc", "-aes-128-ecb", "-K", KEY.hex(), "-nopad"],
        input=block, stdout=subprocess.PIPE, check=True,
    ).stdout


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def pad(blocks):
    return blocks + bytes((-len(blocks)) % 16)


def ccm(aad, plaintext):
    # B_0: Adata=1, (M-2)/2=3, L-1=1. AAD includes its 2-octet length.
    blocks = bytes([0x59]) + NONCE + len(plaintext).to_bytes(2, "big")
    blocks += pad(len(aad).to_bytes(2, "big") + aad) + pad(plaintext)
    auth = bytes(16)
    for offset in range(0, len(blocks), 16):
        auth = aes(xor(auth, blocks[offset:offset + 16]))
    s0 = aes(bytes([1]) + NONCE + bytes(2))
    stream = b"".join(
        aes(bytes([1]) + NONCE + i.to_bytes(2, "big"))
        for i in range(1, (len(plaintext) + 15) // 16 + 1)
    )
    return xor(plaintext, stream) + xor(auth, s0)[:8]


if len(sys.argv) != 2:
    raise SystemExit("usage: ccmp-contract-vectors.py /absolute/path/to/openssl")
for subtype, payload in [(0xC0, b"\x06\x00"), (0xA0, b"\x08\x00"),
                         (0xD0, b"\x08\x00\xab\xcd"),
                         (0xD0, bytes(range(0x31, 0x50)))]:
    aad = bytes([subtype, 0x40]) + ADDRESSES + bytes(2)
    print(f"FC={subtype:02x}40 payload={payload.hex()} aad={aad.hex()} "
          f"ciphertext+mic={ccm(aad, payload).hex()}")
