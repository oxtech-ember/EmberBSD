#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the real imt adapter with userland boundary doubles."""
import contextlib
import os
import re
import shlex
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
sys = here.parents[2]
cc = os.environ.get('CC', 'cc')
if subprocess.check_output([cc, '-dumpversion'], text=True).split('.')[0].strip() != '16':
    raise SystemExit('Use GCC 16 for these target tests.')
directory = os.environ.get('BUILD_DIR')
context = (contextlib.nullcontext(directory) if directory else
           tempfile.TemporaryDirectory(prefix='imt-contract-'))
with context as temp:
    build = Path(temp)
    build.mkdir(parents=True, exist_ok=True)
    # Keep every adapter function. Only replace its kernel include boundary.
    source = '\n'.join(line for line in (here.parent / 'imt.c').read_text().splitlines()
                       if not line.startswith('#include'))
    (build / 'adapter.c').write_text(source)
    command_source = (here.parent / 'ihidev.c').read_text()
    functions = ['ihidev_hid_command', 'ihidev_report_type_conv',
                 'ihidev_get_report', 'ihidev_get_report_poll',
                 'ihidev_set_report', 'ihidev_set_report_poll']
    fragments = [command_source[command_source.index('enum {'):
                                command_source.index('static int I2C_HID_POWER_ON')]]
    for name in functions:
        match = re.search(r'(?m)^(?:static )?int\n' + name + r'\(', command_source)
        if match is None:
            raise SystemExit('Missing command function: ' + name)
        end = command_source.index('\n}\n', match.start()) + 3
        fragments.append(command_source[match.start():end])
    (build / 'commands.c').write_text('\n'.join(fragments))
    for test in ['t_imt', 't_ihidev_report']:
        command = [cc, '-std=c23', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter',
                    *(['-Wno-sign-compare'] if test == 't_ihidev_report' else []),
                    *shlex.split(os.environ.get('CFLAGS', '')),
                    '-I' + str(sys / 'dev/hid/tests/include'), '-I' + str(sys),
                    '-I' + str(build), str(here / (test + '.c')),
                    '-o', str(build / test)]
        subprocess.run(command, check=True)
        if directory:
            preprocess = command.copy()
            preprocess.insert(1, '-E')
            preprocess[-1] = str(build / (test + '.i'))
            subprocess.run(preprocess, check=True)
        subprocess.run([str(build / test)], check=True)
