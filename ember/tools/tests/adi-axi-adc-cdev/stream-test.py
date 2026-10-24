#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the actual stream read path, with deterministic DMA and uio services."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import sys
root = Path(__file__).resolve().parents[4]
text = (root / 'sys/dev/fdt/adi_axi_adc.c').read_text()
functions = []
for name in ('poison_locked', 'adiaxiadc_dma_drain', 'quiesce_locked', 'adiaxiadc_session_release', 'adiaxiadc_stream_notify', 'adiaxiadc_stream_stop', 'adiaxiadc_stream_start', 'adiaxiadc_stream_read', 'adiaxiadc_cdev_poll'):
    kind = 'void' if name in ('poison_locked', 'adiaxiadc_session_release', 'adiaxiadc_stream_notify') else 'int'
    start = text.index('static ' + kind + '\n' + name + '(')
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

    for case in sys.argv[1:] or ['signal', 'selection', 'busy', 'stop', 'all']:
        subprocess.run([str(work / 'test'), case], check=True)
