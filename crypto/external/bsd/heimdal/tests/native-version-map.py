#!/usr/bin/env python3
"""Check the native libkrb5 version-map augmentation."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--awk', required=True, type=Path)
parser.add_argument('--source', required=True, type=Path)
args = parser.parse_args()
script = Path(__file__).resolve().parent.parent / 'lib/libkrb5/native-version-map.awk'
source = (args.source / 'lib/krb5/version-script.map').read_text()
for content, expected in ((source, True),
                          (source.replace('# kinit helper', '# missing'), False),
                          (source + '\nheim_ipc_main;\n', False),
                          (source + '\n# kinit helper\n', False)):
    result = subprocess.run([str(args.awk), '-f', str(script)], input=content,
                            text=True, stdout=subprocess.PIPE, check=False)
    assert (result.returncode == 0) == expected
    if expected:
        assert result.stdout.count('heim_ipc_main;') == 1
print('native version-map tests PASS')
