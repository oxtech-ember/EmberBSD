# SPDX-License-Identifier: BSD-2-Clause
"""Checks for object-root confinement of Waf cleanup."""
import subprocess
import sys
import tempfile
from pathlib import Path

helper = Path(__file__).resolve().parents[1] / 'waf-clean.py'
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp).resolve()
    obj = root / 'obj'
    out = obj / 'custom' / 'build'
    out.mkdir(parents=True)
    (out / 'object.o').write_text('object')
    (obj / 'neighbor').write_text('keep')
    source = root / 'source'
    source.mkdir()
    (source / 'tracked').write_text('keep')
    def clean(path):
        return subprocess.run([sys.executable, str(helper), '--objdir', str(obj),
                               '--output', str(path)], capture_output=True)
    assert clean(out).returncode == 0
    assert not out.exists()
    assert (obj / 'neighbor').read_text() == 'keep'
    assert (source / 'tracked').read_text() == 'keep'
    assert clean(source).returncode != 0
    (obj / 'link').symlink_to(source, target_is_directory=True)
    assert clean(obj / 'link').returncode != 0
    assert clean(obj / 'link' / 'nested').returncode != 0
    assert clean(obj / 'link' / '..' / 'neighbor').returncode != 0
    assert (source / 'tracked').read_text() == 'keep'
print('waf clean confinement PASS')
