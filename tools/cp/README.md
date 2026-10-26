# Initial native cp

The [normal host recipe](Makefile) uses both complete canonical C sources from
[bin/cp](../../bin/cp/Makefile). It links the ordinary OS compatibility library.
It does not select SMALL or omit features. Target ACL and xattr code remains native.

The [finite contract](bootstrap.json) uses separately built filetime and
filemeta components. It does not replace the normal compatibility library.
The selected GCC16 compiles both sources and a mandatory native API control.
There are 35 source-owned cases. Their fixtures remain inside the case directory.
The control requires real descriptor-zero handling, nanosecond timestamps,
file flags, no-follow directory access, symlink times and the public metadata API.
It is not a cp implementation or proof of every cp metadata operation.

Host preservation follows this order: data, ownership, modes, xattrs and ACLs,
timestamps, then file flags. Errors produce a nonzero result. The destination
can contain partial data or metadata after failure. Source files are never removed.
Directory metadata is applied after children. Open source directories must still
match their FTS identity. Explicit -H and -L semantics remain supported.
The shared API overlays metadata. It does not erase destination-only attributes.
Hosts without file flags report ENOTSUP for requested flag preservation; -N retains
its documented meaning. SIGINFO requests progress where available. Other host
builds use SIGUSR1 for the same progress operation.

[The shared stat accessor](../compat/compat_stat_time.h) uses the real normal
configuration or native structure member family. It does not invent HAVE values.
[The source controls](test-source.py) check routing and preserved native code.
Actual GCC16, runtime, C analysis, memory and other-host checks remain separate.
The finite cases cover byte copying, truncation, modes, exact timestamps, recursive
postorder, link selection, hardlink identity, prompts and errors.
The -H case explicitly follows a command-line directory symlink with -p.
Separate Darwin metadata fixtures test nonempty ACLs and xattrs around the real cp.
API controls alone cannot replace those consumer tests.

On macOS arm64 with APFS, GCC 16.2.0 builds both complete sources without warnings.
All 35 behavior cases and four metadata cases pass, including negative metadata controls.
The same 35 cases pass ASan/UBSan with the CP sources instrumented.
The linked compatibility archives are not instrumented in that run.
Leak checking remains unverified because the host LeakSanitizer shutdown deadlocks.
Diagnostic symbolization is disabled because the initial tools lack a symbolizer.

GCC path analysis reports two false positives. In `preserve_host`, its trace changes
an unchanged successful `open` result from nonnegative to negative at cleanup.
In the API control, it rejects a closed destination descriptor for `dup2`.
POSIX permits that destination. Neither diagnostic is suppressed in the source.
Cppcheck remains pending until the common CMake and analyzer dependencies are built.

Device nodes, sparse files, resource exhaustion, hostile concurrent replacement,
remote filesystems remain outside these finite tests.
The implementation retains their canonical operations and reports failures.
The common metadata API rejects FIFO and device metadata paths with ENOTSUP.
Host -p therefore reports failure for those paths. Native target operations remain intact.
The component's unsupported compressed or dataless representations remain explicit.
No full SDK or ordinary cross-host qualification follows from this initial profile.

The integration retains the canonical NetBSD/Berkeley notices. Compare primary
[NetBSD cp](https://github.com/NetBSD/src/blob/trunk/bin/cp/utils.c),
[OpenBSD cp](https://github.com/openbsd/src/blob/master/bin/cp/utils.c),
[FreeBSD cp](https://github.com/freebsd/freebsd-src/blob/main/bin/cp/utils.c), and
[DragonFly cp](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/bin/cp/utils.c).
These implementations guide ordering and FD timestamp use. Their code is not copied.
Apple metadata APIs belong to the shared filemeta component, not this consumer.
