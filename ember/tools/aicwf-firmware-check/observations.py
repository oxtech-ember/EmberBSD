# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; byte observations for the exact original U02 baseline.
import struct

VIF_SIZE = 0x528
VIF_BYTES = 2 * VIF_SIZE
RECORD_BYTES = 28 + 2 * VIF_BYTES


def _store(data, offset, value, count=4):
    data[offset:offset + count] = value.to_bytes(count, 'little')


def expected_key(row, before):
    """Derive the documented original byte effects, not a firmware executor."""
    after = bytearray(before)
    vif = row['vif'] * VIF_SIZE
    context = vif + row['keyid'] * 0x78 + 0x1e0
    request, cipher = row['request'], row['cipher']
    after[context:context + 0x48] = bytes(0x48)
    if cipher == 1:
        after[context + 0x50:context + 0x60] = request[24:40]
    elif cipher == 5:
        after[context + 0x50:context + 0x60] = request[8:24]
    tx = 0xb2c3d4 if cipher in (0, 3) else 0x5c365c365c365c36 if cipher == 4 else 0
    _store(after, context + 0x48, tx, 8)
    after[context + 0x60:context + 0x64] = bytes((cipher, row['keyid'], 0x2a, 1))
    after[context + 0x64:context + 0x74] = request[8:24]
    after[context + 0x74] = request[4]
    _store(after, vif + (0x4b4 if cipher == 5 else 0x4b0), 0x18fb08 + context)
    return bytes(after)


def compare(rows, raw):
    if len(raw) != len(rows) * RECORD_BYTES:
        raise ValueError('Unexpected complete result byte count')
    results = []
    defects = []
    for index, row in enumerate(rows):
        record = raw[index * RECORD_BYTES:(index + 1) * RECORD_BYTES]
        case_id, r0, r1, mask, abi_bad, rng, special = struct.unpack_from('<7I', record)
        before, after = record[28:28 + VIF_BYTES], record[28 + VIF_BYTES:]
        if case_id != index or mask != row['mask'] or abi_bad:
            raise ValueError('ABI, PRIMASK or case identity changed: ' + row['name'])
        context = row['vif'] * VIF_SIZE + row['keyid'] * 0x78 + 0x1e0
        expected = bytearray(before)
        expected_rng = 0
        if row['kind'] in (1, 4):
            expected = expected_key(row, before)
            expected_rng = int(row['cipher'] in (0, 3))
            if row['cipher'] == 5 and (row['initial'] != 0 or row['kind'] == 4):
                defects.append(dict(name=row['name'], boundary='initial-floor-or-same-key-reset'))
        elif row['kind'] == 2:
            candidate = row['pn'] & 0xffffff
            fresh = candidate > row['floor']
            accepted = fresh and not row['bad_mic']
            if r0 != (0 if accepted else 1) or special != (0xa5 if accepted else 0):
                raise ValueError('Original RX return/special differs: ' + row['name'])
            if fresh:
                _store(expected, context, candidate, 8)
            if fresh and row['bad_mic'] or accepted and candidate != row['pn'] or row['pn'] > row['floor'] and not row['bad_mic'] and not accepted:
                defects.append(dict(name=row['name'], boundary='pre-MIC-commit-or-24-bit-truncation'))
        else:
            mic = r0.to_bytes(4, 'little') + r1.to_bytes(4, 'little')
            if mic.hex() != row['expected_mic']:
                raise ValueError('Original CMAC differs from independent oracle: ' + row['name'])
        if bytes(expected) != after or rng != expected_rng:
            raise ValueError('Original complete VIF/stub effects differ: ' + row['name'])
        results.append(dict(name=row['name'], original_observation_matched=True))
    return dict(state='original-instruction-defects-reproduced', accepted_original_fixture=True, accepted_secure_firmware=False, cases=results, original_defect_witnesses=defects, full_firmware_booted=False, irq_interleavings_qualified=False)
