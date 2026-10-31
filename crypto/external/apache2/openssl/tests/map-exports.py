#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test ELF version-map export expectations, including version inheritance."""
import importlib.util
from pathlib import Path
import tempfile

helper = Path(__file__).resolve().parents[1] / 'tools/map-exports.py'
spec = importlib.util.spec_from_file_location('map_exports', helper)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as tmp:
    version_map = Path(tmp) / 'map'
    version_map.write_text('OPENSSL_4.0.0 {\n global:\n old_api;\n};\n'
                           'OPENSSL_4.1.0 {\n global:\n new_api;\n local: *;\n} OPENSSL_4.0.0;\n')
    assert module.expected(version_map).splitlines() == [
        'OPENSSL_4.0.0', 'OPENSSL_4.1.0', 'new_api@@OPENSSL_4.1.0', 'old_api@@OPENSSL_4.0.0']
    version_map.write_text('OPENSSL_4.0.0 {\n global:\n accidental*;\n};\n')
    try:
        module.expected(version_map)
    except ValueError:
        pass
    else:
        raise AssertionError('wildcard export must be rejected')
print('PASS: exact versioned exports and unsupported wildcard rejected')
