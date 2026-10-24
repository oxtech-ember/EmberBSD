#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the generated RX-only RAM-root kernel configuration."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--config', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[4]
profile = root / 'sys/arch/evbarm/conf/PLUTO_NANO_RX'
with tempfile.TemporaryDirectory(prefix='pluto-rx-config-') as work:
    subprocess.run([str(args.config.resolve()), '-s', str(root / 'sys'),
                    '-b', work, str(profile)], check=True)
    output = Path(work)
    md = (output / 'opt_md.h').read_text()
    for option in ('MEMORY_DISK_HOOKS', 'MEMORY_DISK_DYNAMIC', 'MEMORY_DISK_IS_ROOT'):
        assert re.search(r'^#define\s+' + option + r'\s+1$', md, re.M), option
    config = (output / 'ioconf.c').read_text()
    for device in ('ad9361phy', 'adiaxidmac', 'adiaxiadc', 'zynqspi'):
        assert device in config, device
    devsw = (output / 'devsw.c').read_text()
    assert re.search(r'&adiadc_cdevsw,\s*//\s*368\b', devsw), 'Missing RX character device'
print('PASS RX attachments, dynamic RAM root and major 368 registration')
