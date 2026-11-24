#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Run the actual rtw89 beacon-state hook against the net80211 watchdog timer."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('new_work', type=Path)
a = p.parse_args()
a.new_work.mkdir()
source = (a.source / 'sys/dev/pci/rtw89/if_rtw89.c').read_text()
start = source.index('static void\nrtw89_beacon_state(')
end = source.index('\n}\n', start) + 3
(a.new_work / 'beacon-state.inc').write_text(source[start:end])
compiler = shlex.split(os.environ.get('CC', 'cc'))
compiler += shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter'))
binary = a.new_work / 'check'
subprocess.run(compiler + ['-I' + str(a.new_work),
                          str(a.source / 'ember/tests/rtwx-watchdog.c'),
                          '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
