# littlefs integration

This optional filesystem uses the common `devel/littlefs` source prepared by
EmberBSD Ports. Set `EMBER_LITTLEFS_SOURCE` to that prepared tree for a kernel
with `file-system LITTLEFS`, or to include the `littlefs` backend in native
`makefs`. The upstream library is not copied into the OS repository. Its public
symbols are prefixed at compilation so they can coexist with the BSD LFS
filesystem.

The VFS adapter owns vnode identities, permissions, directory operations and
the BSD page-cache interface. One mutex serializes an upstream littlefs
instance. Mount caches and per-vnode file caches have explicit owners;
upstream dynamic allocation is disabled in the kernel. Page I/O uses genfs
with logical file offsets and the upstream file API. The hardware driver
does not implement filesystem or application policy.

Ordinary block devices, including `md` and `vnd`, provide the image-testing
backend. `options LITTLEFS_FLASH` additionally enables the generic `flash(4)`
NOR backend. It uses the selected flash partition's geometry and callbacks,
not a physical address or a board-specific ROM call. NAND is not accepted by
this backend until its ECC and bad-block contract is implemented and tested.
Partition selection belongs to the boot/device configuration; it must keep
the bootloader and other stored data outside the filesystem partition.

## BSD metadata

The data and metadata blocks retain the upstream littlefs format. BSD inode
metadata is an 80-byte user attribute, type `0x42`. Every exposed entry,
including `/`, must have this attribute; missing or invalid metadata is an
I/O error, never an implicit ownership or permission default. All integers
are little endian and are encoded with `littlefs_format.h` rather than by
writing a native structure.

| Byte offset | Value |
| --- | --- |
| 0 | 32-bit magic `0x464c4245` (`EBLF`) |
| 4 | 16-bit version (1), 16-bit length (80) |
| 8 | 64-bit inode number; root is 2 |
| 16, 20, 24, 28 | 32-bit mode, uid, gid, BSD file flags |
| 32 | 64-bit EmberBSD/NetBSD encoded device number |
| 40, 48, 56 | Signed 64-bit access, modification, change time in seconds |
| 64, 68, 72 | 32-bit nanoseconds for those times |
| 76 | Reserved, zero |

Root user attribute `0x43` contains the next inode number as a little-endian
64-bit integer. Allocation persists this counter before creating an inode.
Interrupted allocations can leave unused numbers, which are not reused.

Directories use upstream directory entries. A symbolic link uses a regular
entry with BSD symbolic-link mode; its data is the link target, without a
terminating NUL. Devices, FIFOs and sockets use empty regular entries tagged
with the corresponding BSD type. `makefs` consumes target device numbers
from the normal mtree specification, including `device=netbsd,major,minor`.

The root name `.littlefs-tmp` is reserved and hidden by the VFS adapter.
New entries are created there, given their complete metadata and initial
contents, then published with upstream atomic rename. A read-write mount
removes unfinished staging entries. This prevents a power failure between
upstream create and attribute commit from exposing a file without BSD
permissions. A read-only mount does not perform recovery writes.

Hard links and NFS file handles are unsupported. Image creation rejects hard
links rather than silently duplicating files. BSD extended-attribute and ACL
system calls are also unsupported; internal littlefs user attributes are
not a public xattr namespace.

## Building a root image

Use the normal native `makefs` tool with `-t littlefs`, a target mtree
specification, and an explicit image size. See `makefs(8)` and
`mount_littlefs(8)` for the geometry and mount options. Littlefs does not
compress file data, so image sizing must include metadata and free erase
blocks as well as the file payload.

For example, using the selected native tools and the target's staged root:

```sh
TOOLDIR=/path/to/selected/native/tools
OS_SOURCE=/path/to/os-source
ROOT_TREE=/path/to/staged/root
ROOT_SPEC=/path/to/root.spec
IMAGE=/path/to/root-littlefs.img
IMAGE_SIZE=10m

"$TOOLDIR/bin/nbmakefs" -t littlefs -B little -s "$IMAGE_SIZE" \
    -o block_size=4096,read_size=16,prog_size=16,cache_size=256 \
    -F "$ROOT_SPEC" -N "$OS_SOURCE/etc" "$IMAGE" "$ROOT_TREE"
```

Choose `read_size` and `prog_size` to match the target device. For example,
the generic flash driver reports a one-byte read size and uses its device
program size; the erase block size comes from the device geometry. The
`block_size` passed to `makefs` must match that erase size.

The static network profile accepts `EMBER_NETWORK_ROOT_FILESYSTEM=littlefs`.
This selects `mount_littlefs` and omits the FFS-specific mount/check tools.
It leaves the other service selections unchanged. The generated fstab uses
pass number zero: upstream littlefs performs its own recovery on writable
mounts. An empty `EMBER_NETWORK_ROOT_PARTITION` selects an unpartitioned
device name, such as a generic flash partition device. The default profile
still selects FFS for existing consumers.

`ESP_MOSAICO_LITTLEFS` enables the block-device backend for RAM images and
retains FFS for recovery. `ESP_MOSAICO_LITTLEFS_NOR` enables
`LITTLEFS_FLASH` with the generic flash/SBI NOR backend and disables FFS.
Neither configuration embeds a board partition layout in the VFS; partition
selection belongs to the board profile and boot configuration. Physical NOR
mount, write, and recovery acceptance is still pending.
