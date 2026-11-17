#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Exercise SDMMC PMF callbacks with modeled scheduling and card transport."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('new_work', type=Path)
a = p.parse_args()
a.new_work.mkdir()
bus = (a.source / 'sys/dev/sdmmc/sdmmc.c').read_text()
mem = (a.source / 'sys/dev/sdmmc/sdmmc_mem.c').read_text()
def function(text, name):
    marker = '\n' + name + '('
    pos = text.find(marker)
    if pos < 0:
        return ''
    start = text.rfind('\n\n', 0, pos) + 2
    return text[start:text.index('\n}', pos) + 3] + '\n'
start = bus.index('\n\tif (!pmf_device_register(self,')
end = bus.index('\n\tSET(sc->sc_flags, SMF_INITED);', start)
(a.new_work / 'pmf-register.inc').write_text(bus[start:end])
callbacks = ''.join(function(bus, n) for n in
                    ['sdmmc_pmf_start', 'sdmmc_suspend', 'sdmmc_resume'])
(a.new_work / 'pmf-callbacks.inc').write_text(callbacks)
(a.new_work / 'pmf-reinit.inc').write_text(function(mem, 'sdmmc_mem_reinit'))
(a.new_work / 'pmf-enable.inc').write_text(function(bus, 'sdmmc_enable'))
(a.new_work / 'pmf-mem-init.inc').write_text(function(mem, 'sdmmc_mem_init'))
(a.new_work / 'pmf-worker.inc').write_text(function(bus, 'sdmmc_task_thread'))
binary = a.new_work / 'check'
subprocess.run(shlex.split(os.environ['CC']) +
               shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter')) +
               ['-DHAVE_CALLBACKS=' + str(int(bool(callbacks))),
                '-I' + str(a.new_work), str(a.source / 'ember/tests/sdmmc-pmf.c'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
