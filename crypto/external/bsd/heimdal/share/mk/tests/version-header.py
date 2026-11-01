#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check deterministic version.h generation from Heimdal's selected source."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--awk', required=True, type=Path)
p.add_argument('--source', required=True, type=Path)
p.add_argument('--work-dir', required=True, type=Path)
a = p.parse_args()
for path in (a.awk, a.source / 'configure', a.source / 'configure.ac', a.work_dir):
    if not path.exists():
        p.error('selected input does not exist: ' + str(path))

script = Path(__file__).resolve().parents[1] / 'heimdal-version.awk'
if not script.is_file():
    raise SystemExit('version generator is missing: ' + str(script))

source_ac = (a.source / 'configure.ac').read_text()
match = re.search(r'^AC_INIT\(\[([^\]]+)\],\[([^\]]+)\],', source_ac, re.M)
if match is None:
    raise SystemExit('selected configure.ac has no literal AC_INIT package/version')
package, version = match.groups()
configure = (a.source / 'configure').read_text()
for key, value in (('PACKAGE_NAME', package), ('PACKAGE_VERSION', version)):
    if f"{key}='{value}'" not in configure.splitlines():
        raise SystemExit(f'{key} in selected configure does not match configure.ac')

def render(text):
    with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=a.work_dir,
                                     prefix='heimdal-configure-', delete=False) as source:
        source.write(text)
        input_path = Path(source.name)
    try:
        return subprocess.run([str(a.awk), '-f', str(script), str(input_path)],
                              text=True, capture_output=True, check=False)
    finally:
        input_path.unlink()

current = render(configure)
expected = (
    '#ifndef VERSION_HIDDEN\n'
    '#define VERSION_HIDDEN\n'
    '#endif\n'
    f'VERSION_HIDDEN const char *heimdal_long_version = "@(#)$Version: {package} {version} $";\n'
    f'VERSION_HIDDEN const char *heimdal_version = "{package} {version}";\n'
)
if current.returncode != 0 or current.stdout != expected:
    raise SystemExit('version header differs from selected upstream package/version')

synthetic_version = 'test-' + version
updated = render(configure.replace(f"PACKAGE_VERSION='{version}'",
                                   f"PACKAGE_VERSION='{synthetic_version}'", 1))
expected_updated = expected.replace(f'{package} {version}',
                                    f'{package} {synthetic_version}')
if updated.returncode != 0 or updated.stdout != expected_updated:
    raise SystemExit('version header did not follow a changed package version')

for malformed in (
        "PACKAGE_NAME='Heimdal'\n",
        "PACKAGE_NAME='Heimdal'\nPACKAGE_VERSION='7.8.0/dirty'\n",
        "PACKAGE_NAME='Heimdal'\nPACKAGE_VERSION='7.8.0'\nPACKAGE_VERSION='7.8.1'\n"):
    result = render(malformed)
    if result.returncode == 0 or result.stdout:
        raise SystemExit('malformed configure metadata was accepted')

print(f'Heimdal version header matches {package} {version}; version changes and malformed metadata handled')
