# Bounded FFS1 image verification

This is a read-only acceptance check for small files in a generated EmberBSD
FFS1 image. It is not a filesystem checker, mounter, writer, or general extractor.
## Scope

- Ordinary image files opened read-only, with an explicit 512-byte-aligned offset.
- Little-endian FFS1, 4.4BSD inode format, 4–64 KiB blocks, 512-byte to 8 KiB
  fragments subject to the filesystem's maximum eight fragments per block.
- Root inode 2, cylinder-group inode placement, nested directory records.
- Regular files and directories that fit in 12 direct blocks. Other inode types,
  sparse data, indirect blocks, old inode formats, and other byte orders fail closed.
- At most 64 expected paths, 32 components per path, and a 1 MiB expectation JSON.
- Compare uid, gid, mode, type, size, and file bytes against explicit local inputs.
  It prints neither file contents nor their hashes. Metadata and inode allocation
  outside the selected paths are not an integrity or fsck check.

Image bounds, geometry, inode numbers, directory record lengths, and name lengths
are checked before use. The open image and expected files are checked for changes
during verification. Supply an idle, completed image; do not inspect a file being built.

## Format sources

All offsets follow the current OS sources, not the host's filesystem structures:

- `sys/ufs/ffs/fs.h`: `SBLOCK_UFS1=8192`; `struct fs` has magic at 1372,
  inode format at 1324, inode-block offset at 16, old CG offset/mask at 24/28,
  fragment count at 36, CG count at 44, block/fragment sizes at 48/52,
  fragments per block at 56, indirect entries at 116, inodes per block at 120,
  inodes per group at 184, and fragments per group at 188.
  Address calculation follows `cgstart_ufs1`, `cgimin`, `ino_to_fsba`, and `ino_to_fsbo`.
- `sys/ufs/ufs/dinode.h`: the 128-byte `ufs1_dinode` defines mode at 0,
  byte size at 8, 12 direct pointers at 40, three indirect pointers at 88,
  and modern 32-bit uid/gid at 112/116.
- `sys/ufs/ufs/dir.h`: `struct direct`, 512-byte `UFS_DIRBLKSIZ`, and
  `UFS_DIRECTSIZ` define inode number, record length, type, and name.

Refresh these contracts if the OS disk format changes. The fixture tests use the
actual selected `nbmakefs`, so their positive cases also check these byte offsets.

## Usage

Create an expectation JSON outside Git. For example:

```json
[
  {"path":"/root/.ssh","type":"directory","uid":0,"gid":0,"mode":"0700"},
  {"path":"/etc/wpa_supplicant.conf","type":"file","uid":0,"gid":0,"mode":"0600",
   "source":"/absolute/private-overlay/etc/wpa_supplicant.conf"}
]
```

Add explicit entries for `rc.conf`, `sshd_config`, `authorized_keys`, and the
selected SSH host key. Use the final expected files, not a file from another build.

```sh
"$ruby" ffs1-check.rb --image "$image" --offset 301989888 --expect "$expectations"
"$ruby" ffs1-check-test.rb "$tools/nbmakefs"
```

The tested fixtures use real FFS1 images at offsets 0, 1 MiB, and 288 MiB,
with 4/8/64 KiB blocks and 512/1024/8192-byte fragments. They cover nested
directories, multiple directory sectors and inode blocks, another cylinder
group, full 32-bit ownership, empty files, binary bytes, and the direct-block limit.
Negative controls cover wrong metadata/bytes, wrong offsets, corrupt superblock
fields, out-of-bounds data pointers, invalid directory records, symlinks, and
indirect data. The CLI output is checked for private fixture contents.

Passing fixtures does not prove the final installation image, boot, Wi-Fi, SSH,
or physical board operation. Run the checker against that image separately.
