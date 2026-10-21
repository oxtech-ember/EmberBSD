# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Generate two source-only fixtures. Never compile or run a producer."""
import argparse
import hashlib
import json
from pathlib import Path
import re

WPA_SHA = 'f6e56cdbee6efb0cf0ec884043b404bee0019e1099370a29f5aba405cb84558e'
PATCHED_SHA = '4593bfb536d2821bf5e054c223b22a3eec3846e629c7807ef6d847e2d43212ad'
COMMON_SHA = '6ea1fb91956bd3bec33f0ee89dd094e5fc355f9c8c4079ad0970d8e3d294e3a7'
OLD = '\treturn wpa_cli_cmd(ctrl, "LEVEL", 1, argc, argv);'
NEW = '\treturn wpa_cli_cmd(mon_conn ? mon_conn : ctrl, "LEVEL", 1, argc, argv);'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def function(text, name, static=True):
    prefix = 'static int ' if static else 'int '
    pattern = r'^' + re.escape(prefix + name + '(') + r'.*?^}\n'
    matches = re.findall(pattern, text, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('Function boundary is not unique: ' + name)
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    dist = args.source_root / 'external/bsd/wpa/dist'
    wpa = (dist / 'wpa_supplicant/wpa_cli.c').read_bytes()
    common = (dist / 'src/common/cli.c').read_bytes()
    if sha(wpa) not in (WPA_SHA, PATCHED_SHA) or sha(common) != COMMON_SHA:
        raise ValueError('Pinned source bytes changed')
    text = wpa.decode()
    if sha(wpa) == PATCHED_SHA:
        if text.count(NEW) != 1:
            raise ValueError('Patched function location changed')
        text = text.replace(NEW, OLD, 1)
        if sha(text.encode()) != WPA_SHA:
            raise ValueError('Original source projection changed')
    if text.count(OLD) != 1:
        raise ValueError('Patch location changed')
    patched = text.replace(OLD, NEW, 1)
    shared = function(common.decode(), 'write_cmd', False)
    for name in ('_wpa_ctrl_command', 'wpa_ctrl_command', 'wpa_cli_cmd'):
        shared += '\n' + function(text, name)
    notice = wpa.decode().split('*/', 1)[0] + '*/\n'
    notice += common.decode().split('*/', 1)[0] + '*/\n'
    template = (Path(__file__).parent / 'wpa-cli-level-contract.c.in').read_text()
    args.output.mkdir(mode=0o700)
    receipts = []
    for name, original in [('original', text), ('patched', patched)]:
        level = function(original, 'wpa_cli_cmd_level')
        program = notice + template.replace('/* REAL_FUNCTIONS */', shared + '\n' + level, 1)
        output = args.output / (name + '.c')
        output.write_text(program)
        receipts.append(dict(variant=name, source_path=str(output),
                             sha256=sha(program.encode()),
                             level_function_sha256=sha(level.encode()),
                             source_wpa_sha256=sha(original.encode())))
    receipt = dict(scope='Source generation only. No C compiler or program execution.',
                   fixture_boundary='Per-interface CLI only. ifname_prefix is constant NULL; global IFNAME expansion is not tested.',
                   input_wpa_sha256=sha(wpa), input_common_sha256=sha(common),
                   unchanged_real_helpers=['write_cmd', '_wpa_ctrl_command', 'wpa_ctrl_command', 'wpa_cli_cmd'],
                   protocol_double='Only source-endpoint attachment and synchronous OK/FAIL reply. No real socket or daemon.',
                   variants=receipts)
    (args.output / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
