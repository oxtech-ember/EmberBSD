# libelf source and ABI checks

Run the source-routing check with an explicit BSD make executable:

```sh
python3 external/bsd/elftoolchain/tests/source-routing.py \
    --make /path/to/bmake
```

The check queries BSD make metadata for the generated default source snapshot
and all ten native, host, and CTF consumers. It checks default and absolute
source selection, and confirms that relative, whitespace-containing, and
incomplete overrides fail. Whitespace is unsupported because BSD `.PATH`
cannot safely resolve those source paths. The check uses an empty query target
only. It does not compile or install a target.

Run the ABI probe with an explicit GCC 16 NetBSD target compiler. The runner
checks the compiler banner, target triplet and GCC version before compiling:

```sh
python3 external/bsd/elftoolchain/tests/run-libelf-abi.py \
    --cc /path/to/target-gcc \
    --sysroot /path/to/target-root \
    --expected-base /path/to/base-libelf-expected.json
```

The expected-base manifest is optional. When supplied, the runner verifies its
regular-file payload hashes under the sysroot before it compiles the probe. The
probe checks `STN_UNDEF` and the public `Elf_Data` field layout, links against
`libelf`, writes and reads ELF32 and ELF64 headers, section headers, and section
data through libelf/GElf, and checks refusal of malformed ELF identification.
A cross-built
probe needs an explicit `--runner` command to run on a compatible target. Use
`--include-dir`, `--library-dir`, `--cflag`, and `--ldflag` to pass additional
target-specific arguments.

This probe qualifies only the selected headers, library, compiler, and runtime
environment. It does not qualify other libelf consumers or install a source
provenance marker.
