#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Exercise the USB SD suspend state machine with modeled USB I/O."""
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
source = (a.source / 'sys/dev/usb/urtsx.c').read_text()
def function(name):
    pos = source.find('\n' + name + '(')
    if pos < 0:
        return ''
    start = source.rfind('\n\n', 0, pos) + 2
    return source[start:source.index('\n}', pos) + 3] + '\n'
callbacks = ''.join(function(n) for n in
                    ['urtsx_restore_locked', 'urtsx_suspend', 'urtsx_resume'])
(a.new_work / 'usb-pmf.inc').write_text(callbacks)
start = source.index('\n\tsc->sc_sdmmc = config_found(')
start = source.index('\n', start + 1)
end = source.index('\n\treturn;', start)
(a.new_work / 'usb-register.inc').write_text(source[start:end])
binary = a.new_work / 'check'
subprocess.run(shlex.split(os.environ['CC']) +
               shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter')) +
               ['-DHAVE_CALLBACKS=' + str(int(bool(callbacks))),
                '-I' + str(a.new_work), str(a.source / 'ember/tests/urtsx-pmf.c'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
