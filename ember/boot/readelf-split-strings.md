# Read split DWARF5 strings

The fork's Binutils readelf resolves string indices in GCC16 split objects.
It reads explicit string bases before indexed attributes. Split units without
an explicit base use the contribution header size: 8 for DWARF32 or 16 for
DWARF64. Explicit bases, including zero, and pre-DWARF5 handling are preserved.

The previous reader treated the header as string offsets. The installed Ports
compiler's split-DWARF check failed even though LLVM read the same DWO correctly.
The executable matrix reproduced 6 failures out of 24 cases. The fixed reader
passes all 24 with GCC16.2 and Clang23.1.1 on Apple Silicon macOS.

The regression checks structure/member/variable/base-type names within their
DIEs and the producer. It rejects diagnostics. It covers DWARF4/5, both offset
widths, ordinary objects, followed split objects and standalone DWO files.

```sh
sh ember/tests/readelf-split-strings.sh /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/fixed-readelf /absolute/new-work
```

Use the [owning cross-build path](cross-build.md) for fork host tools.
Update the readelf copied into the external compiler prefix and rerun its
installed split-DWARF check. Do not bypass that check or lower DWARF versions.
This validates the reader and compiler probe, not kernel boot or hardware.

## Provenance

The repair adapts Tom de Vries's
[upstream proposal of 2024-05-30](https://sourceware.org/pipermail/binutils/2024-May/134421.html).
Upstream acceptance of that proposal is not asserted. This adaptation preserves
single attribute consumption, limits implicit defaults to split units,
preserves explicit zero and retains legacy handling below DWARF5.
It is AI-assisted and retains the GNU source's FSF copyright and
[GPL-3.0-or-later license](../../external/gpl3/binutils/dist/COPYING3).
