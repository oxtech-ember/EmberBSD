# Initial file-time compatibility component

[bootstrap-filetime.json](bootstrap-filetime.json) selects the complete
[utimens source](../../lib/libc/gen/utimens.c). The normal compatibility library
uses the same [source fragment](filetime.mk) and [declarations](compat_filetime.h).
Target libc behavior and its original license remain unchanged.

The mandatory control compiles, links and runs against native utimensat.
Two link probes observe native utimens and lutimens. Missing functions select
the canonical reachover bodies. An unavailable utimensat stops this finite route.
Normal compatibility builds omit the reachover bodies when their backend is absent.
Existing tools on those hosts do not gain a false capability.

The generated nbtool_config.h records the actual control and probe observations.
It is a file-time subset, not a full configure result or a Ports libnbcompat header.
The selected GCC16 compiles the source. Selected ar and ranlib build the static archive.
No configure, installer, make, shell hook or previous archive is used.

The source-owned check verifies fixed file timestamps, no-follow symlink timestamps,
an unchanged target and a missing-path error. It removes only its own two fixtures.
The normal and diagnostic runs must both pass. Nanosecond precision, different
filesystems, C analysis and memory checks require separate validation.

The component is an initial OS input only. It does not qualify the complete
OS compatibility library, the separate Ports library, or an ordinary SDK provider.
Metadata and listing components remain separate future source changes.
The [parsedate component](README.parsedate.md) has a separate contract and acceptance.

The native interface follows the existing NetBSD utimens implementation.
[OpenBSD cp](https://github.com/openbsd/src/blob/master/bin/cp/utils.c) and
[FreeBSD cp](https://github.com/freebsd/freebsd-src/blob/main/bin/cp/utils.c)
also use utimensat/futimens. No donor implementation replaces the canonical source.
