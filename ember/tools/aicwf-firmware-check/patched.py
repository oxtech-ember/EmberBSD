# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite desired-state cases and independent BIP observations.
import copy
import struct

import cases
import observations
import oracle

SEED_FIELDS = ('valid', 'cipher', 'length', 'hwindex', 'incoming_hwindex', 'ptk', 'igtk', 'station', 'rx_vif', 'rx_flags', 'key_xor', 'station_flags', 'backup_word')
FRAME_BYTES = 128


def seeded(row, **changes):
    row = copy.deepcopy(row)
    row['seed'] = dict(zip(SEED_FIELDS, (0 if row['kind'] == 1 else 1, 5, 16, 0x2a, 0x2a, 1, 1, 0, row['vif'], 8, 0, 8, 0), strict=True))
    row['seed'].update(changes)
    return row


def make(name, kind=2, floor=0, initial=0, tx=0, pn=1, keyid=4, vif=0, cipher=5, bad=False, key=None, body=b'\x06\x00', fc=0xc0, **seed):
    key = cases.KEY if key is None else key
    request = bytearray(44)
    request[0], request[4] = keyid, 16
    request[8:24] = key
    request[24:30] = initial.to_bytes(6, 'little')
    request[40], request[41] = cipher, vif
    if cipher != 5:
        request[8:40] = bytes(range(32))
        request[4] = {0: 5, 1: 32, 3: 13, 4: 32}.get(cipher, 16)
    frame = bytearray(oracle.frame(keyid, pn, body))
    frame[:2] = fc.to_bytes(2, 'little')
    mic = oracle.bip(cases.KEY, bytes(frame))
    if kind != 3:
        frame[-8:] = bytes([mic[0] ^ int(bad)]) + mic[1:]
    return seeded(dict(name=name, kind=kind, floor=floor, initial=initial, tx=tx, cipher=cipher, keyid=keyid, vif=vif, mask=0, request=bytes(request), frame=bytes(frame), expected_mic=mic.hex(), pn=pn, bad_mic=bad), **seed)


def inputs(irq=False):
    if irq:
        templates = [make('key-new', 1, initial=0x100000001, igtk=2),
                     make('same-lower', 4, floor=100, initial=99, tx=0xffffffffffff, hwindex=7, incoming_hwindex=9, igtk=2),
                     make('same-higher', 4, floor=100, initial=101, tx=0xffffffffffff, hwindex=7, incoming_hwindex=9, igtk=2),
                     make('rx-accept', floor=0xffffffff, pn=0x100000000),
                     make('rx-bad-mic', floor=1, pn=2, bad=True),
                     make('rx-replay', floor=2, pn=2)]
        result = []
        for row in templates:
            for phase in ('before', 'during', 'after'):
                for mask in (0, 1):
                    item = copy.deepcopy(row)
                    item.update(name=row['name'] + '-' + phase + '-mask' + str(mask), mask=mask, irq_phase=phase)
                    result.append(item)
        return result
    rows = [seeded(row) for row in cases.inputs()]
    for initial in (0, 0x1000000, 0xffffffffffff):
        rows.append(make('key5-vif1-initial-' + hex(initial), 1, initial=initial, keyid=5, vif=1, igtk=2))
    for initial in (0x123456789abb, 0x123456789abc, 0x123456789abd):
        for tx in (0, 1, 0xffffffffffff):
            rows.append(make('same-hw-' + hex(initial) + '-tx' + hex(tx), 4, floor=0x123456789abc, initial=initial, tx=tx, hwindex=7, incoming_hwindex=9, igtk=2))
    for word in (1, 2, 3, 4):
        rows.append(make('backup-word-' + str(word), 4, initial=0x100000001, tx=99, backup_word=word, igtk=2))
    for label, change in (('invalid-slot', {'valid': 0}), ('stored-cipher', {'cipher': 4}), ('stored-length', {'length': 15})):
        rows.append(make('key-new-' + label, 4, initial=0xffffffffffff, tx=99, igtk=2, **change))
    rows.append(make('changed-key', 4, initial=0x100000001, tx=99, key=bytes([1]) + cases.KEY[1:], igtk=2))
    for label, change in (('invalid-slot', {'valid': 0}), ('stored-cipher', {'cipher': 4}), ('stored-length', {'length': 15}), ('wrong-key', {'key_xor': 1}), ('station-none', {'station': 255}), ('vif-none', {'rx_vif': 255}), ('no-ptk', {'ptk': 0}), ('no-igtk', {'igtk': 0})):
        rows.append(make('rx-' + label, floor=1, pn=2, **change))
    for label, frame in (('keyid3', oracle.frame(3, 2, b'\x06\x00')), ('keyid6', oracle.frame(6, 2, b'\x06\x00')), ('short', oracle.frame(4, 2, b'\x06\x00')[:41]), ('no-mmie', oracle.frame(4, 2, b'\x06\x00' + bytes(20))[:-18])):
        row = make('rx-' + label); row['frame'] = frame; rows.append(row)
    row = make('rx-corrupt-body', floor=1, pn=2)
    frame = bytearray(row['frame']); frame[24] ^= 1; row['frame'] = bytes(frame); rows.append(row)
    rows.append(make('rx-unclassified', fc=0x80))
    for protected in (0, 0x4000):
        rows.append(make('rx-action-special-' + str(protected), fc=0xd0 | protected, body=b'\x0d\x00'))
        rows.append(make('rx-unicast-protected-' + str(protected), fc=0xc0 | protected, rx_flags=0))
    rows.append(make('rx-unicast-no-pmf', rx_flags=0, station_flags=0))
    rows.append(make('rx-unicast-other-reason', rx_flags=0, body=b'\x05\x00'))
    result = []
    for row in rows:
        for mask in (0, 1):
            item = copy.deepcopy(row); item.update(name=row['name'] + '-mask' + str(mask), mask=mask)
            if len(item['frame']) > FRAME_BYTES:
                raise ValueError('Finite patch frame exceeds observation bound')
            result.append(item)
    return result


def store(data, offset, value, count=4):
    data[offset:offset + count] = value.to_bytes(count, 'little')


def context(row):
    return row['vif'] * observations.VIF_SIZE + row['keyid'] * 0x78 + 0x1e0


def pointer(row, choice):
    if choice == 0:
        return 0
    slot = row['keyid'] if choice == 1 else (5 if row['keyid'] == 4 else 4)
    return 0x18fb08 + row['vif'] * observations.VIF_SIZE + slot * 0x78 + 0x1e0


def before_state(row):
    result = bytearray((index * 17 + 37) & 255 for index in range(observations.VIF_BYTES))
    seed, c = row['seed'], context(row)
    store(result, c, row['floor'], 8); store(result, c + 0x48, row['tx'], 8)
    result[c + 0x50:c + 0x60] = bytes(value ^ seed['key_xor'] for value in cases.KEY)
    result[c + 0x64:c + 0x74] = cases.KEY
    if seed['backup_word']:
        result[c + 0x64 + 4 * (seed['backup_word'] - 1)] ^= 1
    result[c + 0x60:c + 0x64] = bytes((seed['cipher'], row['keyid'], seed['hwindex'], seed['valid']))
    result[c + 0x74] = seed['length']
    for offset, name in ((0x4b0, 'ptk'), (0x4b4, 'igtk')):
        store(result, row['vif'] * observations.VIF_SIZE + offset, pointer(row, seed[name]))
    return bytes(result)


def key_state(row, before):
    result = bytearray(before); c = context(row); request = row['request']
    same = before[c + 0x63] != 0 and before[c + 0x60] == 5 and before[c + 0x74] == 16 and before[c + 0x64:c + 0x74] == request[8:24]
    if row['cipher'] == 5 and same:
        store(result, c, max(int.from_bytes(before[c:c + 8], 'little'), row['initial']), 8)
        result[c + 0x62] = row['seed']['incoming_hwindex']
        return bytes(result), 0
    result[:] = observations.expected_key(row, before)
    result[c + 0x62] = row['seed']['incoming_hwindex']
    if row['cipher'] == 5:
        store(result, c, row['initial'], 8)
    return bytes(result), int(row['cipher'] in (0, 3))


def rx_state(row, before):
    """Evaluate the finite frame contract from bytes and independent CMAC."""
    frame, seed = bytearray(row['frame']), row['seed']
    result = bytearray(before)
    fc, category = int.from_bytes(frame[:2], 'little'), frame[24]
    subtype = fc & 0xfc
    robust = not fc & 12 and (fc & 0xf0 in (0xa0, 0xc0) or fc & 0xf0 == 0xd0 and category not in (4, 7, 11, 15, 22, 127))
    if not robust:
        return bytes(result), 0, 0xa5, bytes(frame)
    if seed['station'] == 255 or seed['rx_vif'] == 255:
        return bytes(result), 1, 0, bytes(frame)
    if not seed['ptk']:
        return bytes(result), 0, 0xa5, bytes(frame)
    if not seed['rx_flags'] & 8:
        if not seed['station_flags'] & 8 or fc & 0x4000:
            return bytes(result), 0, 0xa5, bytes(frame)
        special = int(subtype in (0xc0, 0xa0) and category in (6, 7))
        return bytes(result), 1, special, bytes(frame)
    if subtype == 0xd0 and category in (13, 14):
        return bytes(result), int(not fc & 0x4000), 0xa5, bytes(frame)
    if not seed['igtk']:
        return bytes(result), 0, 0xa5, bytes(frame)
    if len(frame) <= 43:
        return bytes(result), 1, 0, bytes(frame)
    offset, found = 26, None
    while offset + 1 < len(frame):
        length = frame[offset + 1] + 2
        if frame[offset] == 76:
            if length == 18 and offset + length <= len(frame):
                found = offset
            break
        offset += length
    if found is None:
        return bytes(result), 1, 0, bytes(frame)
    keyid = int.from_bytes(frame[found + 2:found + 4], 'little')
    if keyid not in (4, 5):
        return bytes(result), 1, 0, bytes(frame)
    c = seed['rx_vif'] * observations.VIF_SIZE + keyid * 0x78 + 0x1e0
    candidate = int.from_bytes(frame[found + 4:found + 10], 'little')
    current = int.from_bytes(before[c:c + 8], 'little')
    if not before[c + 0x63] or before[c + 0x60] != 5 or before[c + 0x74] != 16 or candidate <= current:
        return bytes(result), 1, 0, bytes(frame)
    mic = bytes(frame[found + 10:found + 18]); frame[found + 10:found + 18] = bytes(8)
    accepted = mic == oracle.bip(before[c + 0x50:c + 0x60], bytes(frame))
    if accepted:
        store(result, c, candidate, 8)
    return bytes(result), int(not accepted), 0xa5 if accepted else 0, bytes(frame)


def expected(row, before):
    if row['kind'] in (1, 4):
        after, rng = key_state(row, before)
        return after, rng, None, 0xa5, row['frame']
    if row['kind'] == 2:
        after, returned, special, frame = rx_state(row, before)
        return after, 0, returned, special, frame
    return before, 0, None, 0xa5, row['frame']


def compare(rows, raw, frames):
    if len(raw) != len(rows) * observations.RECORD_BYTES or len(frames) != len(rows) * FRAME_BYTES:
        raise ValueError('Incomplete patch result/frame bytes')
    results = []
    for index, row in enumerate(rows):
        record = raw[index * observations.RECORD_BYTES:(index + 1) * observations.RECORD_BYTES]
        identity, r0, r1, mask, abi, rng, special = struct.unpack_from('<7I', record)
        before, after = record[28:28 + observations.VIF_BYTES], record[28 + observations.VIF_BYTES:]
        if identity != index or mask != row['mask'] or abi:
            raise ValueError('Patch ABI/PRIMASK/identity mismatch: ' + row['name'])
        if before != before_state(row):
            raise ValueError('Patch deterministic before-state mismatch: ' + row['name'])
        wanted, calls, returned, output, frame = expected(row, before)
        if row['kind'] in (1, 4) and after[context(row) + 0x62] != row['seed']['incoming_hwindex']:
            raise ValueError('Patch HW index mismatch: ' + row['name'])
        if after != wanted or rng != calls or special != output or returned is not None and r0 != returned:
            raise ValueError('Patch complete state/return mismatch: ' + row['name'])
        if row['kind'] == 3 and (r0.to_bytes(4, 'little') + r1.to_bytes(4, 'little')).hex() != row['expected_mic']:
            raise ValueError('Patch original CMAC mismatch: ' + row['name'])
        if frames[index * FRAME_BYTES:index * FRAME_BYTES + len(frame)] != frame:
            raise ValueError('Patch complete MPDU side effect mismatch: ' + row['name'])
        results.append(dict(name=row['name'], desired_observation_matched=True))
    return dict(state='patched-instruction-contract-matched', accepted_patch_fixture=True, accepted_original_fixture=False, accepted_secure_firmware=False, device_or_radio_acceptance=False, full_firmware_booted=False, irq_interleavings_qualified=False, cases=results)
