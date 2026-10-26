#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check only read-only hostname forms and native kernel identity generation."""
import argparse
from pathlib import Path
import socket
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdk-source', 'host-prefix', 'hostname', 'work'):
    parser.add_argument('--' + name, required=True, type=Path)
parser.add_argument('--make', required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(exist_ok=False)
hostname = str(args.hostname.resolve())
query = subprocess.run([args.make, '-C', str(source / 'tools'),
    '-V', '${TOOL_HOSTNAME}', '-V', '${SUBDIR}'], text=True, capture_output=True)
if query.returncode:
    raise SystemExit(query.stdout + query.stderr)
selected, subdirs = query.stdout.splitlines()
if selected != hostname or 'hostname' not in subdirs.split():
    raise SystemExit('ordinary tools graph did not select native hostname')
full = socket.gethostname()
for arguments, expected, status in (([], full + '\n', 0), (['-s'], full.split('.')[0] + '\n', 0),
                                    (['-z'], '', 1)):
    result = subprocess.run([hostname, *arguments], text=True, capture_output=True)
    if result.returncode != status or result.stdout != expected:
        raise SystemExit('native hostname read-only case failed')
    if (not arguments or arguments == ['-s']) and result.stderr:
        raise SystemExit('native hostname read-only case emitted a diagnostic')
    if arguments == ['-z'] and 'usage:' not in result.stderr:
        raise SystemExit('native hostname invalid option did not report usage')
sys.path.insert(0, str(args.sdk_source.resolve() / 'src'))
from emberbsd_sdk.native_os import select_native_os_tools
from emberbsd_sdk.native_os_routing import ensure_native_os_routing
from emberbsd_sdk.inventory import sha256_file
native = select_native_os_tools(str(args.host_prefix.resolve()), str(source))
routing = work / 'commands'
contract = source / 'tools/native-tools/contract'
ensure_native_os_routing(native, contract_path=str(contract),
    contract_sha256=sha256_file(str(contract)), destination=str(routing))
env = dict(PATH=str(routing), TOOL_AWK=native.tool('awk'), TOOL_DATE=native.tool('date'),
           TOOL_HOSTNAME=hostname, LC_ALL='C', USER='fixture', BUILDINFO='native-hostname-regression')
result = subprocess.run([native.tool('sh'), str(source / 'sys/conf/newvers.sh'), '-i', 'FIXTURE'],
                        cwd=work, env=env, text=True, capture_output=True)
(work / 'newvers.log').write_text(result.stdout + result.stderr)
if result.returncode or result.stderr:
    raise SystemExit('kernel identity generation failed; inspect ' + str(work / 'newvers.log'))
text = (work / 'vers.c').read_text()
if '@' + full + ':' not in text or 'native-hostname-regression' not in text:
    raise SystemExit('kernel identity did not use the selected native hostname')
print('native hostname read-only forms and actual kernel identity generation passed')
