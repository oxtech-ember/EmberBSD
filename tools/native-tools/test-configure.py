#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Run the real Texinfo configure header generator with SDK command routing."""

import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--sdk-source', type=Path, required=True)
    parser.add_argument('--host-prefix', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--expect-broken-header', action='store_true')
    args = parser.parse_args()
    sys.path.insert(0, str(args.sdk_source.resolve(strict=True) / 'src'))
    from emberbsd_sdk.native_os import select_native_os_tools
    from emberbsd_sdk.native_os_routing import ensure_native_os_routing
    from emberbsd_sdk.inventory import sha256_file
    source = args.source.resolve(strict=True)
    work = args.work.absolute()
    work.mkdir()
    native = select_native_os_tools(str(args.host_prefix), str(source))
    contract = source / 'tools/native-tools/contract'
    routing = work / 'commands'
    ensure_native_os_routing(native, contract_path=str(contract),
                             contract_sha256=sha256_file(str(contract)),
                             destination=str(routing))
    build = work / 'configure'
    build.mkdir()
    # build.sh intentionally restricts PATH to this complete command mapping.
    env = dict(PATH=str(routing), LC_ALL='C', CONFIG_SHELL=native.tool('sh'),
               SHELL=native.tool('sh'), CC=native.tool('cc'), CXX=native.tool('cxx'))
    configure = source / 'external/gpl2/texinfo/dist/configure'
    with (work / 'configure.log').open('wb') as log:
        result = subprocess.run([native.tool('sh'), str(configure),
                                 '--disable-nls', '--program-prefix=nb'], cwd=build,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
    result.check_returncode()
    text = (build / 'config.h').read_text()
    required = ('#define STDC_HEADERS 1', '#define HAVE_STDLIB_H 1')
    if args.expect_broken_header:
        log = (work / 'configure.log').read_text(errors='replace')
        if all(value in text for value in required) or 'uniq: inaccessible or not found' not in log:
            raise RuntimeError('the incomplete mapping did not reproduce the silently broken header')
        print('Baseline: configure exits zero but loses required standard-header definitions')
        return
    if not all(value in text for value in required):
        raise RuntimeError('configure lost standard-header definitions')
    print('Real Texinfo configure preserves standard-header definitions with the selected SDK tools')


if __name__ == '__main__':
    main()
