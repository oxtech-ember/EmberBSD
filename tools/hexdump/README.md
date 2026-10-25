# Initial hexdump and od host programs

The [normal host target](Makefile) builds the six complete canonical
[hexdump sources](../../usr.bin/hexdump/Makefile) with full OS compatibility.
It installs the prefixed hexdump image and its prefixed od symlink.
The alias uses the documented host installer and executable suffix.
The target build retains its own libutil linkage and od link.
The C entry point selects od syntax from its invocation name.

[bootstrap.json](bootstrap.json) selects the same six C files for initial Darwin GCC16.
It adds the existing [shared efun component](../compat/README.bootstrap.md), not a private library.
The complete four-file closure supplies ecalloc, emalloc and easprintf.
The existing control and five real compiler probes produce nbtool_config.h.
No capability is assumed and no historical compatibility archive is reused.

The SDK finite runner supplies exact source proofs, selected tools and both invocations.
All source files and notices remain unchanged. No format or option is disabled.
The cases use LC_ALL=C and immutable UTF-8 fixtures, including NUL and high bytes.
They cover Ninja's `od -t x1 -A n -v`, exact padding and row boundaries,
duplicate suppression, ELF first-four-byte inspection, skip/length and file concatenation.
The hexdump cases cover custom formats, format files and canonical output.
Invalid options, unsupported types and missing files must fail.
Expected bytes belong to this source contract; they are not captured from the new program.

The normal [NetBSD host route](https://github.com/NetBSD/src/blob/trunk/tools/hexdump/Makefile)
and BSD invocation dispatch remain intact.
[OpenBSD](https://github.com/openbsd/src/blob/master/usr.bin/hexdump/hexdump.c),
[FreeBSD](https://github.com/freebsd/freebsd-src/blob/main/usr.bin/hexdump/hexdump.c) and
[DragonFly](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/usr.bin/hexdump/hexdump.c)
use the same shared-image naming pattern with their own platform integration.

This finite contract does not qualify an ordinary SDK provider or every host and locale.
Actual GCC16 execution, runtime closure, C analysis, memory checks and the Ninja build
remain separate acceptance steps. Source-only checks do not establish those results.
