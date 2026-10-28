# TDB in the base system

This producer builds the library and public header from the complete prepared
`databases/tdb` source and patch set owned by EmberBSD Ports. Select it with
`EMBER_TDB_SOURCE`; the SDK supplies the selected native `HOST_PYTHON`. The
upstream Waf graph builds the target ELF and, when static libraries are
selected, its optional archive from the same target objects. Native bmake
rules install the library, header, links and optional debug information.
The library and header are LGPL-3.0-or-later; upstream utility programs have
separate GPL terms and are not installed by this producer.

The evbarm/aarch64 answers express the supported NetBSD 11 target's libc and
regular-file interfaces without recording a build machine or kernel version.
The relevant ordinary Waf probes and cooperating-process TDB operations were
also run on an existing AArch64 target using FFS and tmpfs; those results do not
cover other file systems, architectures or a newly built release. For another target
or a different execution environment, a caller can select the upstream Waf
answers interface with `EMBER_TDB_CROSS_ANSWERS`, an executor with
`EMBER_TDB_CROSS_EXECUTE`, or both. The source answers file is copied to the
object directory so Waf never modifies the prepared source or OS tree.

Configure receives the selected target compiler, binutils, sysroot and flags;
only the general `-Werror` is removed for configure's feature snippets. The
original C flags, including `-Werror`, are retained for library compilation.

The producer follows the common Ports recipe's `--disable-symbol-versions`
and `--abi-check-disable` options. The former avoids GNU linker version scripts;
the latter leaves upstream's developer-only GDB signature check disabled (it
is also off by default without developer mode). The target build was checked
for its public symbols, SONAME and a linked ordinary consumer; this does not
constitute a complete ABI compatibility audit.
