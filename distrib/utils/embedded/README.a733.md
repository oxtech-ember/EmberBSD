# A733 native image profile

`mkimage -h a733` creates an image with the existing embedded-image mechanism.
It uses vendor boot0 and boot-package files, a native kernel bundle, and an
EmberBSD build's DESTDIR. It does not extract or install a vendor Linux system.

Read the [Zero 3W boot guide](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/boot.md)
for board applicability and recovery limits. The profile does not establish
physical boot, Wi-Fi, or recovery support on a new board revision.

## Inputs

- `-S`: this OS source tree, including `ember/boot/orangepi-zero4-boot.cmd`.
- `-D`: one matching EmberBSD DESTDIR. Its `etc/mtree/NetBSD.dist`, `set.base`,
  `set.comp`, and `set.etc` must exist. These are build manifests, not downloaded
  upstream binary sets. Keep the source revision and build provenance with the image.
- `-K`: one matching kernel build directory with `netbsd` (ELF), `netbsd.img`
  (native ARM64 image), and these DTBs:
  `sun60i-a733-orangepi-zero4.dtb`, `sun60i-a733-orangepi-zero3w.dtb`, and
  `sun60i-a733-orangepi-zero3w-diagnostic.dtb`.
- `A733_BOOT0` and `A733_BOOT_PACKAGE`: explicit paths to verified vendor files
  for the selected board and memory configuration. Preserve their provenance,
  source-image hash, extracted-file hashes, and redistribution terms outside
  this recipe. Structural validation does not authenticate vendor firmware.
- Optional `-c`: a private root-filesystem overlay outside Git. It cannot supply
  `netbsd` or `boot`; the selected kernel directory owns those files.

The profile requires a full root filesystem; `-b` is not supported.
It uses `base comp etc` from DESTDIR and does not import another DTB collection.
The script selects the board DTB. Merely including the diagnostic DTB does not
provide an independently verified diagnostic loader entry.

Select the built host tools explicitly. For example, set `TOOL_MAKEFS`,
`TOOL_GPT`, `TOOL_FDISK`, `TOOL_MTREE`, and `TOOL_MKUBOOTIMAGE` to their full
paths in the selected tools directory. `TOOL_MTREE` must support NetBSD `-C`.
Set `HOST_SH` to the selected host shell for DESTDIR's `dev/MAKEDEV`.

```sh
A733_BOOT0="$assets/boot0.bin" A733_BOOT_PACKAGE="$assets/boot-package.bin" \
    "$HOST_SH" "$src/distrib/utils/embedded/mkimage" \
    -h a733 -S "$src" -D "$destdir" -K "$kernel" \
    -c "$private_overlay" "$output/a733.img"
```

The variables in this example are explicit external paths. No personal network
settings, SSH keys, firmware binaries, or completed images belong in this tree.
New and rebuilt images use mode `0600`. After validating all inputs, the profile
sets an existing regular output to that mode before writing it. Invalid inputs
preserve its bytes and mode. Output symbolic links are rejected before `chmod`.

## Layout and validation

The image uses 512-byte sectors, GPT with 16 entries, and a hybrid MBR.
The first MBR partition exposes FAT32 to the vendor loader.

| Region | Byte offset | Size |
| --- | ---: | --- |
| Primary GPT header and entries | 512 | 2560 |
| boot0 | 8192 | Header length |
| boot-package | 16793600 | Header length |
| FAT32 | 33554432 | 268435456 |
| FFS, label `netbsd-root` | 301989888 | Remaining image space |

Before writing the image, the profile checks required manifests and kernel files,
kernel/DTB signatures, vendor signatures, and little-endian vendor lengths.
Each vendor length must equal its file size and be a positive multiple of 512.
The ranges must not overlap GPT, each other, or FAT. The profile rejects a
device-node output; it builds an ordinary image file.

`populate` wraps the existing boot script with `mkubootimage` and fills FAT.
The optional `finalize_image` hook writes vendor data after GPT and hybrid MBR
creation, before compression. Other profiles use an empty default hook.

Only A733 opts into the root overlay helper. Existing profiles retain their
`-c` behavior. `makefs -r` receives the overlay last, so its file contents win.
The helper appends normalized mtree entries after the set manifests. Those
entries keep file and directory modes but assign `root:wheel`, never host users.
It omits the overlay's `.` entry, so a private overlay directory does not change
the image root's mode. A `0700` `.ssh` and `0600` `authorized_keys` stay private.
The helper does not print file contents. Its temporary metadata is removed by
the existing `mkimage` cleanup trap.

## Checks and limits

```sh
ruby distrib/utils/embedded/a733-test.rb --mock-mtree
ruby distrib/utils/embedded/a733-test.rb --mtree "$tools/nbmtree" \
    --boot0 "$assets/boot0.bin" --boot-package "$assets/boot-package.bin"
```

The check runs the real profile and `mkimage`, real blob writes, and image-tool
test doubles. It checks layout arguments, header lengths, failure before writes,
kernel/DTB placement, and the finalizer's ordering. It uses temporary fixtures,
not a physical card. `--mtree` also checks real mtree duplicate-entry precedence
for `rc.conf`, `sshd_config`, and private SSH modes. `--mock-mtree` checks only
the helper's contract when NetBSD host tools are not yet available.

The source supports final-entry replacement in `usr.sbin/mtree/spec.c`;
`usr.sbin/makefs/makefs.c` merges directories before applying that specification.
These source and fixture checks do not prove filesystem generation or boot.
Before writing a card, use real host tools to build and inspect GPT/MBR, compare
both vendor ranges, inspect FAT files, check FAT in preen mode, and verify FFS
contents and ownership. Then verify the board identity and perform a separate
physical startup check.

Use the [bounded FFS1 reader](README.ffs1-check.md) to compare small configuration
files and private-key permissions with explicit local expectations. It reads
the completed image without mounting it and does not print file contents.
Its real `nbmakefs` fixtures cover metadata, bytes, and malformed-input refusal.
It is not a filesystem integrity check or physical startup acceptance.
