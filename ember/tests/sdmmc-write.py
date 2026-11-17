#!/usr/bin/env python3
# Origin: EmberBSD (AI-assisted).
"""Exercise real SD write completion, splitting and block-device retry code."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('new_work', type=Path)
parser.add_argument('--case', choices=['retry', 'status', 'timeout', 'all'], default='all')
args = parser.parse_args()
args.new_work.mkdir()

def extract(text, name):
    marker = '\n' + name + '('
    start = text.index(marker)
    start = text.rfind('static ', 0, start)
    end = text.index('\n}\n', start + 1) + 3
    return text[start:end] + '\n'

memory = (args.source / 'sys/dev/sdmmc/sdmmc_mem.c').read_text()
(args.new_work / 'sdmmc-write-subr.inc').write_text(extract(memory, 'sdmmc_mem_write_block_subr'))
(args.new_work / 'sdmmc-write-single.inc').write_text(extract(memory, 'sdmmc_mem_single_write_block'))
disk = (args.source / 'sys/dev/sdmmc/ld_sdmmc.c').read_text()
(args.new_work / 'sdmmc-write-ld.inc').write_text(extract(disk, 'ld_sdmmc_dobio'))
selected = []
for text in [memory, disk,
             (args.source / 'sys/dev/sdmmc/sdmmcvar.h').read_text(),
             (args.source / 'sys/dev/sdmmc/sdmmcreg.h').read_text(),
             (args.source / 'sys/sys/buf.h').read_text()]:
    lines = iter(text.splitlines())
    for line in lines:
        definition = line
        while line.endswith('\\'):
            line = next(lines)
            definition += '\n' + line
        match = re.match(r'#define\s+(\w+)', definition)
        if match and (match.group(1).startswith(('SCF_', 'SMC_CAPS_', 'SMF_', 'SFF_',
                                                'SD_R1_', 'MMC_R1', 'MMC_ARG_RCA',
                                                'SDMMC_SECTOR_SIZE')) or
                      match.group(1) in ['MMC_WRITE_BLOCK_SINGLE', 'MMC_WRITE_BLOCK_MULTIPLE',
                                         'MMC_STOP_TRANSMISSION', 'MMC_SEND_STATUS',
                                         'SD_APP_SET_WR_BLK_ERASE_COUNT', 'B_READ',
                                         'LD_SDMMC_IORETRIES', 'RECOVERYTIME']):
            selected.append(definition)
(args.new_work / 'sdmmc-write-flags.inc').write_text('\n'.join(selected) + '\n')
compiler = shlex.split(os.environ['CC'])
# Match the kernel's warning policy for inherited int/size_t block loops.
compiler += shlex.split(os.environ.get('CFLAGS', '-O2 -g -std=c11 -Wall -Wextra -Werror -Wno-sign-compare'))
binary = args.new_work / 'check'
subprocess.run(compiler + ['-I' + str(args.new_work),
                           str(args.source / 'ember/tests/sdmmc-write.c'),
                           '-o', str(binary)], check=True)
for case in ['retry', 'status', 'timeout'] if args.case == 'all' else [args.case]:
    subprocess.run([str(binary), case], check=True)
