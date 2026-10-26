# Native host file metadata

`filemeta.mk` selects one implementation for normal tools/compat and the finite
`compat-filemeta-bootstrap` component. Target libc, cp, and mv are unchanged.
The API composes public native calls. It does not call `copyfile` or `fcopyfile`.

## Contract

`compat_filemeta.h` declares two functions with explicit `NB_FILEMETA_XATTR` and
`NB_FILEMETA_ACL` masks. Zero or unknown bits fail with `EINVAL`.

The FD function accepts open regular files and directories, including FD zero.
It retains caller ownership. The path function does not follow final symlinks.
Directories use `O_DIRECTORY | O_NOFOLLOW`. Regular paths require an actual
`O_REGULAR` capability. Otherwise, use the FD API or receive `ENOTSUP`.
Special files are rejected before opening them. `O_EVTONLY` is not a safe
type-restricted open: native vnode open can still call a device driver's open.
Neither function confines symlinked ancestors or promises atomic path snapshots.

Xattrs overlay source names and values. Destination-only attributes remain.
Zero-length values are copied. Lengths, list boundaries, and short writes are
checked. Size changes have at most three attempts. Operational errors propagate.
BSD selects the user namespace and, for real UID zero, the system namespace.
This matches the canonical extattr copy policy; it does not claim other namespaces.

Nontrivial ACLs retain their order and flags. Empty or absent source ACLs leave
destination ACLs unchanged. BSD copies a nonempty default directory ACL separately.
Darwin uses filesec property queries to distinguish absence from native failure.
Its complete returned ACL is copied through the FD setter. A required symlink ACL
write fails with `ENOTSUP`; no following path setter is used.

When both parts are requested, xattrs precede ACLs. Failure preserves the first
errno through cleanup. Partial destination metadata can remain. The functions
never copy file contents, remove paths, print errors, or promise transactions.
Compressed and dataless representations are rejected when native flags identify
them. A Darwin compression attribute also fails explicitly. No flag is replaced
with zero to obtain a successful build.

Ownership, final modes, times, and flags remain caller policy. Future cp/mv callers
must finish data writes first, apply ownership before final modes, and flags last.
The component does not qualify EXDEV moves or their child cp/rm invocations.

## Native interfaces and shared headers

The eight source-owned probes use exact native pointer types and actual link
outcomes. Normal configure and the finite build consume those same probe files.
No host-name check selects a successful backend. Unsupported operations fail.
The two BSD setter probes distinguish the native `int` and `ssize_t` declarations.

The common restricted NetBSD header prefix precedes the scoped metadata adapter.
Only that adapter exposes required extended declarations. `_NETBSD_SOURCE` does
not reach compat_defs or other translation units. FD/open/xattr probes do not
depend on ACL headers. The ACL probe selects the shared ACL adapter explicitly.
Darwin's shared ACL acquisition helper also serves the listing component.

## Finite acceptance

The manifest fixes 18 commands: one control compile/link/run, eight probes,
one implementation compile, archive/index, and check compile/link/run/runtime.
The archive contains only `compat_filemeta.o`. Four public exports comprise the
archive, generated `nbtool_config.h`, `compat_filemeta.h`, and `bootstrap_cdefs.h`.
Internal adapters remain exact source inputs. They are not public configuration.

The Darwin profile requires actual open, ACL, xattr, and representation probes.
Other probe results remain recorded. The required native control establishes
allocation and ACL iteration conventions before feature failures are interpreted.

The semantic check uses independent native getters for binary and empty xattrs,
replacement, destination-only names, two-entry ACL order, directory inheritance,
FD zero, directory paths, source ACL absence, protected symlink targets, and errors.
Fixtures have fixed names and are removed without following links. Both executables
require full runtime traces. Runtime evidence records the actual filesystem type.

## Copy behavior test

`probes/filemeta-fixture-support.h` owns shared diagnostic setters and independent
getters. The component check and `probes/filemeta-fixture.c` use this same code.
The fixture does not call the metadata-copy API, `cp`, or a shell.
It accepts only `prepare|observe|observe-move` and fixed case names from `filemeta-fixture.json`.
Its file names are fixed as `src` and `dst` in the current test directory.

Build `probes/filemeta-fixture.c` with the selected native SDK GCC16 tuple.
Use `-Wall -Wextra -Werror` and the tuple's native ABI and driver options.
Then run the test with explicit executable paths and a new work directory:

```sh
"$SDK_PYTHON" tools/compat/test-copy-metadata.py \
    --fixture "$FILEMETA_FIXTURE" --cp "$SDK_CP" --work-root "$NEW_TEST_DIRECTORY"
```

The four cases test binary and empty xattrs, replacement, destination-only xattrs,
empty-source ACL overlay, ordered ACL entries, and directory inheritance flags.
Each case must fail observation before copying and pass after the real `cp` call.
Directory copying uses `cp -Rp src/. dst` to update the existing destination.
The runner also checks file data and directory contents. It retains command logs.
The work directory must be fresh; the runner never removes an existing directory.
The runner retains explicit `ASAN_OPTIONS` and `UBSAN_OPTIONS` for instrumented
fixtures. A run with leak detection disabled does not check memory leaks.

These tests currently require Darwin ACL and xattr interfaces. They do not prove
symlink metadata, other filesystems, EXDEV moves, allocation faults, or memory safety.
They are behavior tests, not an admission requirement for an installed tool.

Source checks do not prove native support. C compilation, ordinary NetBSD and
other BSD builds, Cppcheck, path analysis, memory checks, allocation faults, other
filesystems, and cross-filesystem moves require separate evidence.

## Provenance

The namespace overlay follows canonical `lib/libc/gen/extattr.c`. The public BSD
ACL interfaces preserve canonical cp semantics without private ACL layouts.
The full cp/mv source and its licenses remain with their native owners.

Primary API and behavior references:

* [Apple xattrs](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/xattr.h).
* [Apple ACL and filesec ownership](https://github.com/apple-oss-distributions/Libc/blob/71bbe350ab79eef58113991d817ccc6165061a64/posix1e/acl_file.c).
* [Apple vnode open](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/vfs/vfs_vnops.c).
* [Apple device open](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/miscfs/specfs/spec_vnops.c).
* [FreeBSD FD and directory ACL handling](https://github.com/freebsd/freebsd-src/blob/175595ba6c5253e395c028e792787dffd2875910/bin/cp/utils.c).
* [OpenBSD caller error handling](https://github.com/openbsd/src/blob/3bcfcfa94a9add1af97390415bdb705ef073f8d7/bin/cp/utils.c).
* [DragonFly caller behavior](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/b639e42052d8492e5d665956ec8218900a9c8ec7/bin/cp/utils.c).

This is new EmberBSD API composition. No donor implementation was copied.

### Cross-device move observation

`observe-move` supports the three regular-file cases only. It refuses the directory case.
It checks source xattrs and ACLs, including an empty source ACL.
It requires the destination-only xattr to be absent after replacement.
The existing `observe` operation retains the copy overlay contract.

Prepare each case in fresh directories on two different filesystems.
The destination directory needs its own `src` identity witness and initial `dst`.
This witness is not the moved source; expected metadata comes from fixed native fixture values.
Require `observe-move` to fail before moving the primary source to the other filesystem's `dst`.
Then require source removal, exact destination data, and a successful `observe-move`.
Use a real rename probe to require `EXDEV` before these checks.
The fixture invokes neither `mv` nor the metadata-copy API.
