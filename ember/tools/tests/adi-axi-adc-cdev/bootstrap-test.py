#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute actual first-CONFIG admission and provider bootstrap functions."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[4]
source = (root / 'sys/dev/fdt/adi_axi_adc.c').read_text()
functions = []
for marker in ('int\nadi_axi_adc_bootstrap(', 'static bool\nadiaxiadc_same_rx_path(', 'static int\nadiaxiadc_cdev_ioctl('):
    start = source.index(marker)
    end = source.index('\n}\n', start) + 3
    functions.append(source[start:end])
with tempfile.TemporaryDirectory(prefix='adi-adc-bootstrap-') as directory:
    work = Path(directory)
    header = (root / 'sys/dev/fdt/adi_axi_adc.h').read_text()
    (work / 'abi.inc').write_text('\n'.join(line for line in header.splitlines() if not line.startswith('#include')))
    (work / 'bootstrap.inc').write_text('\n'.join(functions))
    command = [os.environ['CC'], '-std=c11', '-Wall', '-Wextra', '-Werror']
    command += shlex.split(os.environ.get('CFLAGS', ''))
    subprocess.run(command + ['-I' + str(work), str(Path(__file__).with_suffix('.c')), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
