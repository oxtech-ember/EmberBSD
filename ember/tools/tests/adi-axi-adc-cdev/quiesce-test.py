#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[4]
text = (root / 'sys/dev/fdt/adi_axi_adc.c').read_text()
name = 'adiaxiadc_cdev_quiesce'
# Include the legacy misspelled definition when reproducing the old failure.
old = 'adi_axi_adc_cdev_quiesce'
actual = name if '\n' + name + '(void *cookie' in text else old
start = text.index('static int\n' + actual + '(void *cookie')
end = text.index('\n}\n', start) + 3
function = text[start:end].replace(old, name)
with tempfile.TemporaryDirectory(prefix='adi-adc-quiesce-') as work:
    work = Path(work)
    (work / 'quiesce.inc').write_text(function)
    cmd = [os.environ['CC'], '-std=c11', '-Wall', '-Wextra', '-Werror']
    cmd += shlex.split(os.environ.get('CFLAGS', ''))
    cmd += ['-I' + str(work), str(Path(__file__).with_suffix('.c')), '-o', str(work / 'test')]
    subprocess.run(cmd, check=True)
    subprocess.run([str(work / 'test')], check=True)
