#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual RPCAP implementation with TLS disabled and enabled.

--cflags-json supplies the selected target/configuration flags as an argv
array (without a compiler, source or output). No network code is executed.
Optional --baseline checks the original no-TLS diagnostic and byte-identical
TLS machine code; it requires the selected --objcopy.
"""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path,
                        help='prepared common Ports libpcap source directory')
    parser.add_argument('--cflags-json', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--objcopy', type=Path)
    args = parser.parse_args()
    if not args.cc.is_absolute() or not args.cc.is_file():
        parser.error('--cc must be an existing absolute compiler')
    source = args.source / 'pcap-rpcap.c'
    if not args.source.is_absolute() or not source.is_file():
        parser.error('--source must be a prepared libpcap source directory')
    if args.baseline and (not args.baseline.is_file() or not args.objcopy or
                          not args.objcopy.is_absolute() or not args.objcopy.is_file()):
        parser.error('--baseline requires an existing source and absolute --objcopy')
    flags = json.loads(args.cflags_json.read_text())
    if not isinstance(flags, list) or not all(isinstance(v, str) for v in flags):
        parser.error('--cflags-json must be an argv array')
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    commands = []

    def run(argv, diagnostic=None):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        commands.append(dict(argv=argv, status=result.returncode, output=output))
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if diagnostic:
            if not result.returncode or diagnostic not in output:
                raise RuntimeError('baseline did not reproduce expected diagnostic: ' + output)
        elif result.returncode:
            raise RuntimeError(output)

    for tls in (False, True):
        mode = ['-UHAVE_OPENSSL'] + (['-DHAVE_OPENSSL'] if tls else [])
        output = work / ('tls.o' if tls else 'plain.o')
        run([str(args.cc), *flags, *mode, '-c', str(source), '-o', str(output)])
        if args.baseline:
            baseline = work / ('baseline-tls.o' if tls else 'baseline-plain.o')
            run([str(args.cc), *flags, *mode, '-c', str(args.baseline), '-o', str(baseline)],
                None if tls else 'Wunused-but-set-global')
            if tls:
                for path in (output, baseline):
                    run([str(args.objcopy), '--dump-section=.text=' + str(path.with_suffix('.text')),
                         str(path)])
                if output.with_suffix('.text').read_bytes() != baseline.with_suffix('.text').read_bytes():
                    raise RuntimeError('TLS implementation machine code changed')
    print('RPCAP TLS enabled/disabled compilation: PASS')


if __name__ == '__main__':
    main()
