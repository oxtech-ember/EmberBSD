#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source firmware ownership checks.
"""Run firmload and driver consumers with modeled file and allocator services."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', help='Read consumers from a Git revision.')
    parser.add_argument('--consumers-only', action='store_true')
    parser.add_argument('--work', required=True, type=Path,
                        help='Existing parent for temporary test outputs.')
    parser.add_argument('--keep', action='store_true')
    args = parser.parse_args()

    def source(path):
        if args.revision:
            return subprocess.check_output(
                ['git', '-C', str(root), 'show', args.revision + ':' + path],
                text=True)
        return (root / path).read_text()

    selections = [('sys/dev/firmload.c', 'int', 'firmware_read'),
                  ('sys/dev/ic/bwfm.c', 'static void', 'bwfm_firmware_read_file'),
                  ('sys/dev/sdmmc/if_aicwf.c', 'static int', 'aicwf_fw_read')]
    if not args.consumers_only:
        selections.insert(0, ('sys/dev/firmload.c', 'int', 'firmware_read_alloc'))
    else:
        # A changed consumer still needs the real shared helper.
        if 'firmware_read_alloc(' in source('sys/dev/sdmmc/if_aicwf.c'):
            selections.insert(0, ('sys/dev/firmload.c', 'int', 'firmware_read_alloc'))
    parts = []
    for path, result, name in selections:
        text = source(path)
        match = re.search(r'^' + re.escape(result + '\n' + name) +
                          r'\([^;]*?\n\{.*?^\}', text, re.M | re.S)
        if not match:
            raise SystemExit('Cannot find complete function: ' + name)
        parts.append(text[:text.index('#include')] + '\n' + match[0])

    work = Path(tempfile.mkdtemp(prefix='firmload-', dir=args.work))
    try:
        (work / 'dev').mkdir()
        (work / 'dev/firmload.h').write_text(source('sys/dev/firmload.h'))
        (work / 'firmload-under-test.c').write_text('\n'.join(parts) + '\n')
        command = shlex.split(os.environ.get('CC', 'gcc-16')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-g']
        command += shlex.split(os.environ.get('FIRMLOAD_CFLAGS', ''))
        if args.consumers_only:
            command += ['-DCONSUMERS_ONLY']
        command += ['-I' + str(work), str(root / 'ember/tools/firmload-contract.c'),
                    '-o', str(work / 'contract')]
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get('TEST_RUNNER', '')) +
                       [str(work / 'contract')], check=True)
    finally:
        if args.keep:
            print('Test outputs: ' + str(work), flush=True)
        else:
            import shutil
            shutil.rmtree(work)


if __name__ == '__main__':
    main()
