# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; literal original-defect witnesses and independent MIC inputs.
import oracle

KEY = bytes(range(16))


def inputs():
    rows = []
    def add(name, kind, floor=0, initial=0, tx=0, pn=1, bad=False, cipher=5, keyid=4, vif=0, mask=0, body=b'\x06\x00'):
        request = bytearray(44)
        request[0], request[4] = keyid, 16
        request[8:24] = KEY
        request[24:30] = initial.to_bytes(6, 'little')
        request[40], request[41] = cipher, vif
        if cipher != 5:
            request[8:40] = bytes(range(32))
            request[4] = {0: 5, 1: 32, 3: 13, 4: 32}.get(cipher, 16)
        frame = oracle.frame(keyid, pn, body)
        mic = oracle.bip(KEY, frame)
        if kind != 3:
            frame = frame[:-8] + bytes([mic[0] ^ (1 if bad else 0)]) + mic[1:]
        rows.append(dict(name=name, kind=kind, floor=floor, initial=initial, tx=tx, cipher=cipher, keyid=keyid, vif=vif, mask=mask, frame=frame, request=bytes(request), expected_mic=mic.hex(), pn=pn, bad_mic=bad))
    for initial in (0, 1, 0xffffff, 0x1000000, 0xffffffff, 0x100000000, 0xffffffffffff):
        add('initial-' + hex(initial), 1, initial=initial)
    for floor, pn, bad in ((0, 1, False), (1, 1, False), (1, 2, True), (0, 0x1000000, False), (0, 0x100000001, False), (0xffffffff, 0x100000000, False), (0xffffffffffff, 0, False)):
        add('rx-' + hex(floor) + '-' + hex(pn) + ('-bad-mic' if bad else ''), 2, floor=floor, pn=pn, bad=bad)
    add('rx-key5-vif1', 2, keyid=5, vif=1)
    add('rx-mask1-bad', 2, floor=1, pn=2, bad=True, mask=1)
    for floor, initial, tx in ((100, 2, 1), (100, 100, 0xffffffffffff), (100, 101, 7)):
        add('same-key-' + str(initial), 4, floor=floor, initial=initial, tx=tx)
    for cipher in (0, 1, 2, 3, 4, 6, 255):
        add('generic-' + str(cipher), 1, cipher=cipher)
    for size in (2, 14, 15, 16, 17, 30, 65):
        add('cmac-body-' + str(size), 3, body=bytes((index * 7 + 6) & 255 for index in range(size)))
    return rows


def emit(rows):
    result = ['/* Generated fixture inputs. Synthetic keys only. */', '#define CASE_COUNT ' + str(len(rows))]
    for index, row in enumerate(rows):
        result.append('static const u8 frame_' + str(index) + '[] = {' + ','.join(str(value) for value in row['frame']) + '};')
    result.append('static const struct test_case test_cases[CASE_COUNT] = {')
    for index, row in enumerate(rows):
        numbers = [row['kind'], row['floor'] & 0xffffffff, row['floor'] >> 32, row['tx'] & 0xffffffff, row['tx'] >> 32, row['keyid'], row['vif'], row['mask'], len(row['frame'])]
        result.append(' {' + ','.join(str(value) + 'U' for value in numbers) + ',frame_' + str(index) + ',{' + ','.join(str(value) for value in row['request']) + '}},')
    result.append('};')
    return '\n'.join(result) + '\n'
