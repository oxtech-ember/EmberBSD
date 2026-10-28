#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Build a private EDK2 package overlay using prepared shared codec sources."""
import argparse
import os
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
for name in ('edk2', '7zip', 'brotli', 'output'):
    parser.add_argument('--' + name, required=True, type=Path)
args = parser.parse_args()
for path in (args.edk2, args.__dict__['7zip'], args.brotli, args.output):
    if not path.is_absolute():
        parser.error('all paths must be absolute')
source = args.edk2.resolve(strict=True) / 'MdeModulePkg'
seven = args.__dict__['7zip'].resolve(strict=True)
brotli = args.brotli.resolve(strict=True)
output = args.output
if os.path.lexists(output):
    parser.error('overlay output already exists')
module_name = Path('Library/LzmaCustomDecompressLib')
module = output / 'MdeModulePkg' / module_name
module.mkdir(parents=True)
for name in ('LzmaCustomDecompressLib.inf', 'LzmaDecompressLib.uni',
             'LzmaDecompress.c', 'GuidedSectionExtraction.c',
             'LzmaDecompressLibInternal.h', 'UefiLzma.h', 'EmberLzmaGlue.h'):
    src = source / module_name / name
    if src.is_symlink() or not src.is_file():
        parser.error('missing regular EDK2 module input: ' + str(src))
    shutil.copy2(src, module / name)
(module / 'Sdk').mkdir()
(module / 'Sdk/C').symlink_to(seven / 'C', target_is_directory=True)
package = output / 'MdeModulePkg'
shutil.copy2(source / 'MdeModulePkg.dec', package / 'MdeModulePkg.dec')
for name in ('Include', 'Test', 'Core', 'MdeModulePkg.uni'):
    (package / name).symlink_to(source / name)
path = package / 'Library/BrotliCustomDecompressLib'
path.mkdir(parents=True)
(path / 'brotli').symlink_to(brotli, target_is_directory=True)
print(output)
