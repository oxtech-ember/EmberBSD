#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the actual stream read path, with deterministic DMA and uio services."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[4]
text = (root / 'sys/dev/fdt/adi_axi_adc.c').read_text()
functions = []
for name in ('adiaxiadc_stream_stop', 'adiaxiadc_stream_start', 'adiaxiadc_stream_read', 'adiaxiadc_cdev_poll'):
    start = text.index('static int\n' + name + '(')
    end = text.index('\n}\n', start) + 3
    functions.append(text[start:end])
with tempfile.TemporaryDirectory(prefix='adi-stream-') as work:
    work = Path(work)
    (work / 'stream.inc').write_text('\n'.join(functions))
    cmd = [os.environ['CC'], '-std=c11', '-Wall', '-Wextra', '-Werror']
    cmd += shlex.split(os.environ.get('CFLAGS', ''))
    if os.environ.get('ANALYZE') == 'yes':
        cmd += ['-fanalyzer']
    subprocess.run(cmd + ['-I' + str(work), str(Path(__file__).with_suffix('.c')), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
