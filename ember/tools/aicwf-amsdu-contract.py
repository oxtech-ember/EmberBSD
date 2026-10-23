#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source RX ownership checks.
"""Compile the real aicwf RX consumer and common net80211 A-MSDU parser."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    if platform.system() == 'Darwin' and not os.environ.get('SDKROOT'):
        os.environ['SDKROOT'] = subprocess.check_output(
            ['xcrun', '--show-sdk-path'], text=True).strip()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', help='Read the implementation from Git.')
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--keep', action='store_true')
    args = parser.parse_args()

    def source(path):
        if args.revision:
            return subprocess.check_output(
                ['git', '-C', str(root), 'show', args.revision + ':' + path],
                text=True)
        return (root / path).read_text()

    text = source('sys/dev/sdmmc/if_aicwf.c')
    parts = [text[:text.index('#include')]]
    for name in ('AICWF_RXHDR_LEN', 'AICWF_RXHDR_STATUS', 'AICWF_RX_DECR',
                 'AICWF_RX_DECR_WEP', 'AICWF_RX_DECR_WAPI',
                 'AICWF_RXHDR_FLAGS', 'AICWF_RX_FLAG_UPLOAD',
                 'AICWF_RX_FLAG_REORDER', 'AICWF_RX_FLAG_AMSDU'):
        match = re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.M)
        if not match:
            raise SystemExit('Missing descriptor definition: ' + name)
        parts.append(match[0])
    for name in ('aicwf_rx_input', 'aicwf_rx_free', 'aicwf_rx_ether',
                 'aicwf_amsdu_read', 'aicwf_rx_amsdu', 'aicwf_rx_data'):
        match = re.search(r'^static [^\n]+\n' + name +
                          r'\([^;]*?\n\{.*?^\}', text, re.M | re.S)
        if match:
            prefix = text[text.rfind('\n}', 0, match.start()) + 2:match.start()]
            body = match[0]
            if name == 'aicwf_amsdu_read':
                # The production callback must match the shared void * ABI.
                body = body.replace(name + '(',
                    '// cppcheck-suppress constParameterCallback\n' + name + '(', 1)
            parts.append(prefix + body)
        elif name not in ('aicwf_amsdu_read', 'aicwf_rx_amsdu'):
            raise SystemExit('Missing complete consumer function: ' + name)
    work = Path(tempfile.mkdtemp(prefix='aicwf-amsdu-', dir=args.work))
    try:
        (work / 'sys').mkdir()
        (work / 'net80211').mkdir()
        (work / 'sys/systm.h').write_text('#include <string.h>\n#include <stdint.h>\n')
        for name in ('ieee80211.h', 'ieee80211_amsdu.h', 'ieee80211_amsdu.c'):
            (work / 'net80211' / name).write_text(source('sys/net80211/' + name))
        (work / 'aicwf-rx-under-test.c').write_text('\n'.join(parts) + '\n')
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                 '-D_DEFAULT_SOURCE', '-D_DARWIN_C_SOURCE', '-I' + str(work)]
        flags += shlex.split(os.environ.get('AMSDU_CFLAGS', ''))
        compiler = shlex.split(os.environ.get('CC', 'gcc-16'))
        sources = [root / 'ember/tools/aicwf-amsdu-contract.c',
                   work / 'net80211/ieee80211_amsdu.c']
        database = [{'directory': str(root), 'file': str(path),
                     'arguments': compiler + flags + ['-c', str(path)]}
                    for path in sources]
        (work / 'compile_commands.json').write_text(json.dumps(database, indent=2))
        subprocess.run(compiler + flags + [str(p) for p in sources] +
                       ['-o', str(work / 'contract')], check=True)
        subprocess.run(shlex.split(os.environ.get('TEST_RUNNER', '')) +
                       [str(work / 'contract')], check=True)
    finally:
        if args.keep:
            print('Test outputs: ' + str(work), flush=True)
        else:
            shutil.rmtree(work)


if __name__ == '__main__':
    main()
