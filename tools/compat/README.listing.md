# Initial listing compatibility component

[bootstrap-listing.json](bootstrap-listing.json) selects four complete translation units.
The normal compatibility library uses the same [source fragment](listing.mk).
The getbsize, humanize_number and stat_flags bodies keep their original licenses.
The target libc and libutil recipes and behavior remain unchanged.

[compat_listing.h](compat_listing.h) prefixes host formatter symbols and declares
canonical HN flag values from [stdlib.h](../../include/stdlib.h).
Native formatter probes record availability. They never select a different host HN ABI.
Both normal and finite builds compile the same canonical formatting bodies.
Normal configuration still selects its existing string and stat compatibility support.
No SMALL mode or reduced library replaces these sources.

[compat_acl.c](compat_acl.c) provides `__nbcompat_acl_present_nofollow(path, present)`.
Success stores exactly zero or one and preserves the caller's errno.
Failure preserves the output argument and returns minus one with a native error.
It never follows the final path symlink. It does not expose native ACL storage layouts.
The BSD backend preserves the [ls ACL selection](../../bin/ls/print.c):
NFS4, then ACCESS, with device and whiteout exclusion.
An unavailable backend returns ENOTSUP. It never reports an unsupported ACL as absent.

[compat_acl_features.h](compat_acl_features.h) opens the NetBSD ACL header namespace
only for the shared backend probe and its implementation.
The normal configuration selects POSIX headers before this adapter runs.
Restricted types, endian, time and parameter headers are loaded first.
This prevents duplicate fallback definitions in compat_defs.h.
The adapter then loads native stat, ACL and unistd declarations with a local
`_NETBSD_SOURCE` definition. It removes that definition before compat_defs.h.
Caller-supplied `_NETBSD_SOURCE` remains an error. Other host namespaces remain unchanged.
The finite Darwin configuration includes the same adapter, which does nothing on Darwin.
This build-only header is a source input, not a public component export.

The Darwin backend uses public lstatx_np and filesec property APIs.
An absent FILESEC_ACL property means no ACL. A failed path lookup remains an error.
A present property must produce a valid ACL before its first entry is inspected.
This distinction avoids treating all ENOENT errors from acl_get_link_np as absence.
The empty-ACL entry convention is also checked by the required native control.

The shared six declaration/link probes observe native formatters, stat flags,
and the BSD and Darwin ACL backends. The finite Darwin route requires stat flags
and its Darwin ACL backend. The native control also checks string and locale primitives.
Generated configuration records actual results; it is not a full configure result.
No assumed host-capability values are supplied.

The fixed route has 19 commands: three control commands, six probes, four source
compiles, archive/index, and four check commands. It retains two runtime traces.
The archive contains exactly getbsize.o, humanize_number.o, stat_flags.o and compat_acl.o.
Five exports are libnbcompat.a, nbtool_config.h, compat_listing.h, compat_acl.h,
and bootstrap_cdefs.h. The SDK validates source evidence and all preceding outputs
before each child. It uses selected GCC16, ar and ranlib, without make or configure.

The check covers BLOCKSIZE, canonical HN formatting and flag parsing.
Expected HN cases come from the [libc tests](../../tests/lib/libc/gen/t_humanize_number.c).
It creates only listing-file and listing-link, plus a required absent listing-missing.
A native fd query independently proves the ACL-bearing fixture.
The shared API then checks empty and present ACLs, no-follow symlink behavior,
an unchanged target, and a missing-path error. Cleanup removes only owned fixtures.
The normal and diagnostic check runs must both succeed.

This is an initial OS component, not complete OS or Ports libnbcompat qualification.
It adds no ls consumer. Native execution, other filesystems, full Unicode/NLS,
C analysis and memory validation remain separate requirements.

## API provenance

The BSD policy follows canonical NetBSD ls and
[FreeBSD ls](https://github.com/freebsd/freebsd-src/blob/main/bin/ls/print.c).
[OpenBSD ls](https://github.com/openbsd/src/blob/master/bin/ls/print.c) and
[DragonFly ls](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/bin/ls/print.c)
were reviewed; their absence of this ACL indicator does not remove EmberBSD behavior.
Darwin semantics follow Apple's public
[ACL entry source](https://github.com/apple-oss-distributions/Libc/blob/main/posix1e/acl_entry.c),
[ACL file source](https://github.com/apple-oss-distributions/Libc/blob/main/posix1e/acl_file.c),
and [filesec source](https://github.com/apple-oss-distributions/Libc/blob/main/gen/filesec.c).
The backend composes these public APIs. No Apple implementation is copied.
