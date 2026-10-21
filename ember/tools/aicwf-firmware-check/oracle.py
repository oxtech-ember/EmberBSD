# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; independently authored AES-CMAC fixture oracle.
# Specification: RFC 4493, sections 2 and 4. No vendor code or tables.
from functools import lru_cache


def _multiply(a, b):
    result = 0
    for _ in range(8):
        if b & 1:
            result ^= a
        a = ((a << 1) ^ (0x11b if a & 0x80 else 0)) & 255
        b >>= 1
    return result


def _substitute(value):
    inverse, base, exponent = 1, value, 254
    while exponent:
        if exponent & 1:
            inverse = _multiply(inverse, base)
        base = _multiply(base, base)
        exponent >>= 1
    value = inverse if value else 0
    return value ^ ((value << 1 | value >> 7) & 255) ^ ((value << 2 | value >> 6) & 255) ^ ((value << 3 | value >> 5) & 255) ^ ((value << 4 | value >> 4) & 255) ^ 0x63


_SBOX = tuple(_substitute(value) for value in range(256))


@lru_cache(maxsize=8)
def _round_keys(key):
    if not isinstance(key, bytes) or len(key) != 16:
        raise ValueError('AES-128 requires a 16-byte key')
    result, rcon = list(key), 1
    while len(result) < 176:
        word = result[-4:]
        if len(result) % 16 == 0:
            word = [_SBOX[value] for value in word[1:] + word[:1]]
            word[0] ^= rcon
            rcon = _multiply(rcon, 2)
        for value in word:
            result.append(result[-16] ^ value)
    return result


def aes(key, block):
    if not isinstance(block, bytes) or len(block) != 16:
        raise ValueError('AES requires a 16-byte block')
    schedule = _round_keys(key)
    state = [value ^ schedule[index] for index, value in enumerate(block)]
    for round_number in range(1, 11):
        state = [_SBOX[state[row + 4 * ((column + row) % 4)]] for column in range(4) for row in range(4)]
        if round_number < 10:
            mixed = []
            for column in range(4):
                a, b, c, d = state[column * 4:column * 4 + 4]
                mixed += [_multiply(a, 2) ^ _multiply(b, 3) ^ c ^ d,
                          a ^ _multiply(b, 2) ^ _multiply(c, 3) ^ d,
                          a ^ b ^ _multiply(c, 2) ^ _multiply(d, 3),
                          _multiply(a, 3) ^ b ^ c ^ _multiply(d, 2)]
            state = mixed
        state = [value ^ schedule[round_number * 16 + index] for index, value in enumerate(state)]
    return bytes(state)


def _double(block):
    value = int.from_bytes(block, 'big')
    return (((value << 1) & ((1 << 128) - 1)) ^ (0x87 if value >> 127 else 0)).to_bytes(16, 'big')


def _xor(left, right):
    return bytes(a ^ b for a, b in zip(left, right, strict=True))


def cmac(key, message):
    if not isinstance(message, bytes):
        raise ValueError('CMAC message must be bytes')
    k1 = _double(aes(key, bytes(16)))
    k2 = _double(k1)
    blocks = max(1, (len(message) + 15) // 16)
    tail = message[(blocks - 1) * 16:]
    last = _xor(tail, k1) if len(tail) == 16 else _xor(tail + b'\x80' + bytes(15 - len(tail)), k2)
    state = bytes(16)
    for offset in range(0, (blocks - 1) * 16, 16):
        state = aes(key, _xor(state, message[offset:offset + 16]))
    return aes(key, _xor(state, last))


def frame(keyid, pn, body):
    if not 0 <= keyid <= 65535 or not 0 <= pn < 1 << 48 or not isinstance(body, bytes):
        raise ValueError('Invalid BIP fixture input')
    header = b'\xc0\x00\x00\x00' + bytes.fromhex('ffffffffffff0200000000010200000000020000')
    return header + body + b'\x4c\x10' + keyid.to_bytes(2, 'little') + pn.to_bytes(6, 'little') + bytes(8)


def bip(key, mpdu):
    if len(mpdu) < 42 or mpdu[-18:-16] != b'\x4c\x10':
        raise ValueError('BIP fixture needs a complete appended MMIE')
    fc = (int.from_bytes(mpdu[:2], 'little') & ~0x3800).to_bytes(2, 'little')
    aad = fc + mpdu[4:22]
    return cmac(key, aad + mpdu[24:-8] + bytes(8))[:8]
