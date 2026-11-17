#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Run the actual USB SD callback with the transport model in urtsx-host.c."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('new_work', type=Path)
args = parser.parse_args()
args.new_work.mkdir()
source = (args.source / 'sys/dev/usb/urtsx.c').read_text()
start = source.index('static void\nurtsx_exec_command(')
end = source.index('\nstatic void\n', start + 1)
(args.new_work / 'urtsx-exec.inc').write_text(source[start:end] + '\n')
start = source.index('static void\nurtsx_poll(')
end = source.index('\nstatic ', start + 1)
(args.new_work / 'urtsx-poll.inc').write_text(source[start:end] + '\n')
def extract(name):
    marker = '\n' + name + '('
    start = source.index(marker)
    start = source.rfind('static ', 0, start)
    end = source.find('\nstatic ', start + 1)
    if end < 0:
        end = len(source)
    return source[start:end] + '\n'
helpers = extract('urtsx_host_ocr')
for name in ['urtsx_power_off_locked', 'urtsx_legacy_setup_locked']:
    if '\n' + name + '(' in source:
        helpers += extract(name)
(args.new_work / 'urtsx-power-helpers.inc').write_text(helpers)
(args.new_work / 'urtsx-power.inc').write_text(extract('urtsx_bus_power'))
(args.new_work / 'urtsx-detach.inc').write_text(extract('urtsx_detach'))
flags = (args.source / 'sys/dev/sdmmc/sdmmcvar.h').read_text()
selected = [line for line in flags.splitlines() if re.match(r'#define\s+SCF_', line)]
registers = (args.source / 'sys/dev/sdmmc/sdmmc_ioreg.h').read_text()
selected.extend(line for line in registers.splitlines()
                if re.match(r'#define\s+SD_IO_SEND_OP_COND\s', line))
registers = (args.source / 'sys/dev/sdmmc/sdmmcreg.h').read_text()
selected.extend(line for line in registers.splitlines()
                if re.match(r'#define\s+MMC_OCR_3_[23]V_', line))
(args.new_work / 'sdmmc-flags.inc').write_text('\n'.join(selected) + '\n')
compiler = shlex.split(os.environ['CC'])
compiler += shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror'))
binary = args.new_work / 'check'
subprocess.run(compiler + ['-I' + str(args.source / 'sys/dev/usb'),
                           '-I' + str(args.new_work),
                           str(args.source / 'ember/tests/urtsx-host.c'),
                           str(args.source / 'sys/dev/usb/urtsx_proto.c'),
                           '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
