#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[4]
text=(root/'sys/dev/fdt/adi_axi_adc.c').read_text()
functions=[]
for marker in ('int\nadi_axi_adc_acquire(', 'static struct adi_axi_adc_softc *\nadiaxiadc_cdev_lookup('):
 start=text.index(marker);end=text.index('\n}\n',start)+3
 functions.append(text[start:end])
with tempfile.TemporaryDirectory(prefix='adi-adc-state-') as work:
 work=Path(work);(work/'provider-state.inc').write_text('\n'.join(functions))
 cmd=[os.environ['CC'],'-std=c11','-Wall','-Wextra','-Werror']+shlex.split(os.environ.get('CFLAGS',''))
 subprocess.run(cmd+['-I'+str(work),str(Path(__file__).with_suffix('.c')),'-o',str(work/'test')],check=True)
 for case in sys.argv[1:] or ['acquire','lookup']:subprocess.run([str(work/'test'),case],check=True)
