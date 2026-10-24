#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the receive-disabled FDT RAM-root kernel profile."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--config', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[4]
profile = root / 'sys/arch/evbarm/conf/PLUTO_NANO_RAMDISK'
assert profile.is_file(), 'Missing dynamic RAM-root profile'
with tempfile.TemporaryDirectory(prefix='pluto-ramdisk-config-') as work:
    subprocess.run([str(args.config.resolve()), '-s', str(root / 'sys'),
                    '-b', work, str(profile)], check=True)
    output = Path(work)
    md = (output / 'opt_md.h').read_text()
    for option in ('MEMORY_DISK_HOOKS', 'MEMORY_DISK_DYNAMIC', 'MEMORY_DISK_IS_ROOT'):
        assert re.search(r'^#define\s+' + option + r'\s+1$', md, re.M), option
    assert not re.search(r'^#define\s+MEMORY_DISK_ROOT_SIZE\b', md, re.M)
    config = (output / 'ioconf.c').read_text()
    assert 'mdattach' in config
    for device in ('ad9361phy', 'adiaxidmac', 'adiaxiadc', 'zynqspi'):
        assert device not in config, device
print('PASS FDT dynamic RAM-root config with no radio attachments')
