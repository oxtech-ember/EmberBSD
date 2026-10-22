# FFS2 extended-attribute list formats

## Problem and scope

The native FFS2 attribute store supports two list formats.
BSD `extattr_list_*` requests `EXTATTR_LIST_LENPREFIX` and receives a length byte before each name.
The `listxattr`, `llistxattr`, and `flistxattr` API requests flag zero and requires NUL-terminated names.
The byte count is the same for both formats.

At baseline `50fbd499eea3ce1b47dc10db2ac7fe604237fee3`, `ffs_listextattr` ignored the format flag.
A CM5 check on a separate FFS2ea image confirmed the error.
An attribute named `user.ember-abi` produced these 15 bytes through `llistxattr`:

```text
0e757365722e656d6265722d616269
```

The first byte is the name length; the final NUL is absent.
The same image passed attribute create, duplicate-create rejection, replace, and BSD get/list operations.
`libattr` correctly treats the first byte as part of a name and fails its subsequent attribute lookup.
This failure belongs to the OS list implementation, not the Ports library.

The repair keeps BSD length-prefixed output and its existing truncation behavior.
For flag zero, it copies the name and an explicit NUL.
The on-disk name has no guaranteed NUL or zero padding.
A buffer too small for a complete name returns `ERANGE`.
UFS1 delegation, namespace selection, permission checks, and error cleanup remain unchanged.

## Actual-source regression

The host contract extracts `ffs_listextattr` from the supplied source.
It uses the real on-disk structure and namespace/format constants.
Its doubles model vnode access and `uiomove`, including partial BSD copies and injected copy failures.
Run from the repository root with Ruby and a C11 compiler that supports ASan and UBSan:

```sh
work=/absolute/new-ffs-contract
mkdir "$work"
ruby ember/tests/ffs-xattr-list.rb "$PWD" \
    "$PWD/sys/ufs/ffs/ffs_extattr.c" "$work/fixed"
git show 50fbd499eea3ce1b47dc10db2ac7fe604237fee3:sys/ufs/ffs/ffs_extattr.c > "$work/baseline.c"
if ruby ember/tests/ffs-xattr-list.rb "$PWD" "$work/baseline.c" "$work/baseline"; then
    echo 'Unexpected baseline success' >&2
    exit 1
fi
```

Inspect the logs: the baseline must compile and fail the `memcmp` list-format assertion.
A build failure does not prove the regression.
The repaired code passes 101 and 102 checks, with UFS1 attribute support disabled and enabled.
Cases include exact-size buffers, 255-byte names, namespace filtering, empty lists, BSD truncation,
an incomplete second name, access/open failures, both copy failures, and unsupported vnode types.
The runner saves command arrays and input SHA256 values.

## Validation boundary

GCC 16.2 `-fanalyzer` compiled the full baseline and repaired module with the actual AArch64 EMBER64 kernel options and headers.
Both passed with the existing warnings and `-Werror`; the repaired list function uses 64 bytes of stack.
The stack warning threshold remains 3584 bytes.
Cppcheck 2.22 reported the same 33 inherited findings in both module runs, with no new findings.
These include existing const/scope suggestions and architecture-header reports.
An existing `ffs_close_ea` finding is separate: `ffs_truncate` at line 535 can have its error overwritten by `ffs_extwrite` at line 538.
The OS FFS owner must review that commit-path failure; this list repair does not change it.

Apple Clang 21 Static Analyzer reports no path errors in the contract.
Contract Cppcheck reports const suggestions for the preserved kernel-style double signatures and the inherited `eaend` variable.
ASan/UBSan pass on Apple Silicon macOS; Valgrind is not supported on that execution host.
No Valgrind result is claimed.

A complete kernel build, boot, and physical retest of the repair are pending.
The source contract does not exercise the syscall wrapper, locking, disk I/O, or a mounted filesystem.
Installed `attr` acceptance must repeat the real copy and CLI operations on FFS2ea after that kernel boot.
A filesystem without extended-attribute support must remain an explicit unsupported result.
Do not change the device root filesystem to run this check.
