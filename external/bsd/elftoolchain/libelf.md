# Canonical libelf source

EmberBSD-Ports owns the libelf source pin and patches in
`pkgsrc/devel/elftoolchain`. The recipe combines the stable ELF Tool Chain
archive with the BSD changes required by this tree. The native OS make rules
retain the existing `libelf.so.2` ABI.

`libelf-source/` is a generated snapshot. It contains the prepared `common/`
and `libelf/` directories, exact `ports.distinfo`, and `libelf-source.json`.
Do not maintain patches in this snapshot. Change the Ports recipe and generate
a new snapshot with the EmberBSD-SDK `scripts/prepare-libelf.py` command.
Review and validate the prepared root before replacing the snapshot.

The receipt binds the archive, patches, Ports revision, expected file inventory
and prepared tree hash. To check a snapshot independently, use the SDK
`emberbsd_sdk.libelf.validate_source` API. Pass a clean Ports checkout at the
recorded revision to also check its recipe. These checks establish source
identity. They do not establish runtime or ABI acceptance.

`libelf-source.mk` selects this bundled snapshot by default. Native target
libraries, headers and host CTF tools use the same source selection. A caller
can set `EMBER_LIBELF_SOURCE` to an absolute prepared root. It must have the
same validated structure and remain unchanged for the complete build. Validate
external inputs before and after the producer. Build objects separately from
the source root. No target architecture or host path is fixed by this choice.

The native `lib/libelf` source list and exports remain unchanged. The existing
headers declare `elf_getversion`, but the native source list does not include
its implementation and the inspected base library does not export the symbol.
This API remains pre-existing and unqualified for native use. The Ports
`Version.map` applies to the Ports library and does not qualify native exports.

The library installs `ports.distinfo` as
`/usr/share/emberbsd/ports/elftoolchain.distinfo`. Consumers use this marker to
select the qualified base provider. Do not install another libelf in a package
prefix. A source marker alone does not qualify an existing binary.

The `dist/libdwarf` sources remain separate. This snapshot does not claim their
provenance. Their build rules use the selected common headers and libelf ABI.
