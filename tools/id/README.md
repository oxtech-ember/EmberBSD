# Native ID and group compatibility

Origin: EmberBSD (AI-assisted). The ID source retains its BSD copyright notices.

[bootstrap.json](bootstrap.json) selects the complete [ID implementation](../../usr.bin/id/id.c).
The [normal host recipe](Makefile) retains the shared libnbcompat configuration
and the common internal [group adapter](../../common/include/libutil/compat_grouplist.h).
The ordinary OS recipe and its tests use that same header. It adds no installed library ABI.
There is no bootstrap-only ID implementation or second compatibility library.
The normal host configuration supplies missing BSD declarations and functions,
including `getprogname` and `setprogname` on hosts such as glibc Linux.
The fixed Darwin profile uses its accounted native libc without that library.
The target recipe retains `groups`, `whoami` and every ID option.
The SDK runner creates those same invocation aliases for the native profile.

Darwin declares `getgrouplist` with an `int` base and `int *` output.
The adapter calls that public API with a separate integer array.
It converts values to `gid_t` storage and preserves high unsigned group IDs.
It casts no array pointer and uses no private membership API.
Other hosts call their native `gid_t` interface directly.
The existing libnbcompat `grp.h` supplies missing name lookup declarations;
it does not provide this signature adapter.

Before any source header, ID requests `_DARWIN_UNLIMITED_GETGROUPS` on Apple hosts.
This selects the public full-membership `getgroups` interface for macOS 10.6 and later.
The source does not enable all Darwin extensions with `_DARWIN_C_SOURCE`.
Other hosts retain their native group API.
The finite profile's forced annotation header includes only `sys/cdefs.h`.
It does not declare `getgroups` before the source can select its interface.
See Apple's [public unistd header](https://github.com/apple-oss-distributions/Libc/blob/main/include/unistd.h).

ID obtains the actual supplementary count with `getgroups(0, NULL)`.
It allocates the required space and retries if membership grows before retrieval.
An empty supplementary list still permits the primary group.
Named-user lookup also reallocates from the reported required count.
Allocation, API errors and continuously changing membership fail explicitly.
Both paths permit at most eight attempts. Neither relies on `_SC_NGROUPS_MAX`.

The source review compared NetBSD ID revision 1.32, OpenBSD revision 1.32,
FreeBSD `usr.bin/id/id.c`, and DragonFly BSD `usr.bin/id/id.c`.
Those implementations retain fixed group bounds or a sysconf allocation.
This repair keeps EmberBSD's output rules and uses the actual-count approach.
Sources: [NetBSD](https://github.com/NetBSD/src/blob/trunk/usr.bin/id/id.c),
[OpenBSD](https://github.com/openbsd/src/blob/master/usr.bin/id/id.c),
[FreeBSD](https://github.com/freebsd/freebsd-src/blob/main/usr.bin/id/id.c),
[DragonFly BSD](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/usr.bin/id/id.c).

The fixed profile has 31 read-only cases and 34 commands.
Independent SDK Python `os`, `pwd` and `grp` observations supply identity expectations.
The source contract contains only the public `root` and numeric `0` subjects.
Personal names, IDs and memberships remain in local execution evidence.
Cases cover real/effective IDs, names, complete groups, named-user lookup,
pretty output, aliases and fixed syntax errors. They change no credentials.
Native acceptance must check the complete list, including membership above 16 groups.
Current-session `-p` login handling remains unchanged and has separate OS tests.
Historical verification retains accepted streams instead of querying current identity again.

Run `tools/id/tests/source.py` with the explicit SDK Python for source checks.
[grouplist.c](tests/grouplist.c) is a separate typed-ABI C diagnostic fixture.
The normal OS ID tests also cover count growth, empty lists and API failures.
Source and synthetic tests do not establish C correctness.
Actual GCC16 builds, diagnostic fixture execution, OS tests, C analysis,
memory checks and native runtime acceptance remain pending.
