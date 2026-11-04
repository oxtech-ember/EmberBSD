# Common zlib source

The base `libz.so.1`, installer `zcat`, and standalone consumers use the source
prepared from the common Ports `devel/zlib` recipe through `EMBER_ZLIB_SOURCE`.
The selector names an absolute prepared source tree; there is no embedded
fallback. `sys/lib/libsa` requires it only when `SA_USE_CREAD=yes`.

The common NetBSD patch preserves the existing `libz.so.1` offset ABI,
including LP32 behavior, and the kernel and standalone interfaces. A prior
Ports zlib 1.3.2 built with the upstream `long long` default can be ABI
incompatible on LP32; rebuild affected consumers for the common NetBSD ABI.
The base version and exported symbol list remain owned by `shlib_version` and
`z.expsym`. `zlib.pc` derives its version from the selected source header.

Run `tools/native-tools/test-zlib-source.py` with selected OS make, compiler,
SDK source, prepared zlib and target sysroot to check routing, bounded refresh,
the generated package metadata and a focused target syntax build. The Ports
source owner supplies the separate LP32/LP64 and standalone ABI regression.
