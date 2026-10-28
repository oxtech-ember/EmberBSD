#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Exercise generated EDK2 codec paths without changing source trees."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    base = Path(temporary)
    edk2 = base / 'edk2'
    package = edk2 / 'MdeModulePkg'
    module = package / 'Library/LzmaCustomDecompressLib'
    module.mkdir(parents=True)
    for name in ('LzmaCustomDecompressLib.inf', 'LzmaDecompressLib.uni',
                 'LzmaDecompress.c', 'GuidedSectionExtraction.c',
                 'LzmaDecompressLibInternal.h', 'UefiLzma.h', 'EmberLzmaGlue.h'):
        (module / name).write_text(name + '\n')
    (package / 'MdeModulePkg.dec').write_text('DEC\n')
    for name in ('Include', 'Test', 'Core'):
        (package / name).mkdir()
    (package / 'MdeModulePkg.uni').write_text('UNI\n')
    seven, brotli, output = base / '7zip', base / 'brotli', base / 'overlay'
    (seven / 'C').mkdir(parents=True)
    (brotli / 'c/include').mkdir(parents=True)
    command = [sys.executable, '-B', str(root / 'ember/tools/firmware-codec-overlay.py'),
               '--edk2', str(edk2), '--7zip', str(seven), '--brotli', str(brotli),
               '--output', str(output)]
    subprocess.run(command, check=True, capture_output=True)
    overlay_module = output / 'MdeModulePkg/Library/LzmaCustomDecompressLib'
    assert (overlay_module / 'Sdk/C').resolve() == (seven / 'C').resolve()
    assert (output / 'MdeModulePkg/Library/BrotliCustomDecompressLib/brotli').resolve() == brotli.resolve()
    assert (overlay_module / 'LzmaDecompress.c').read_text() == 'LzmaDecompress.c\n'
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert (module / 'LzmaDecompress.c').read_text() == 'LzmaDecompress.c\n'
print('firmware codec overlay: PASS')
