#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Run actual rtw89 PMF registration through the shared network lifecycle."""
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
source = (a.source / 'sys/dev/pci/rtw89/if_rtw89.c').read_text()
start = re.search(r'\n\tif \(!?pmf_device_register\(self, NULL, NULL\)\)', source).start()
end = source.index('\n}', start)
(a.new_work / 'driver-registration.inc').write_text(source[start:end] + '\n')
source = (a.source / 'sys/kern/kern_pmf.c').read_text()
start = source.index('static bool\npmf_class_network_suspend(')
end = source.index('\nbool\npmf_event_inject(', start)
(a.new_work / 'network-pmf.inc').write_text(source[start:end] + '\n')
compiler = shlex.split(os.environ['CC'])
compiler += shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter'))
binary = a.new_work / 'check'
subprocess.run(compiler + ['-I' + str(a.new_work),
                          str(a.source / 'ember/tests/rtwx-pmf.c'),
                          '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
