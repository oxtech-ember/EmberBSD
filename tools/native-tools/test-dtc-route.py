#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check selected DTC routing and the native tools graph."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
prefix = args.prefix.resolve(strict=True)
with tempfile.TemporaryDirectory() as temporary:
    route = Path(temporary) / 'route'
    route.mkdir()
    (route / 'dtc').symlink_to(prefix / 'bin/dtc')
    common = [str(prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
              '-C', str(root / 'tools'), 'MAKECONF=/dev/null', '_SRC_TOP_=',
              'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm',
              'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
              'TOOLDIR=' + str(Path(temporary) / 'tools')]
    env = dict(os.environ, TOOL_DTC='/wrong/environment/dtc')
    def query(*extra):
        return subprocess.run([*common, *extra, '-V', '${TOOL_DTC}',
                               '-V', '${SUBDIR}'], env=env, check=True,
                              capture_output=True, text=True).stdout.splitlines()
    selected = query('EMBER_NATIVE_TOOLS=' + str(route))
    assert selected[0] == str(route / 'dtc'), selected
    assert 'dtc' not in selected[1].split(), selected
    assert 'libfdt' not in selected[1].split(), selected
    legacy = query()
    assert legacy[0].endswith('/bin/nbdtc'), legacy
    assert {'dtc', 'libfdt'} <= set(legacy[1].split()), legacy
    assert 'dtc dtc -\n' in (root / 'tools/native-tools/contract').read_text()
    version = subprocess.run([selected[0], '--version'], check=True,
                             capture_output=True, text=True).stdout
    assert version.startswith('Version: DTC '), version
    source = Path(temporary) / 'sample.dts'
    output = Path(temporary) / 'sample.dtb'
    source.write_text('/dts-v1/; / { compatible = "emberbsd,test"; };\n')
    subprocess.run([selected[0], '-I', 'dts', '-O', 'dtb', '-o', str(output),
                    str(source)], check=True)
    assert output.read_bytes()[:4] == bytes.fromhex('d00dfeed')
print('native DTC routing and graph: PASS')
