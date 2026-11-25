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
    --update-python "$HOST_PREFIX/bin/python3.15" \
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

## Zero 3W two-slot image

Select `-h a733-update` for the stock EMBER64 minimal no-LDAP profile with development tools.
Use the matching native boot bundle. Supply `modules/NAME.kmod` for its four modules under `-K`.
The update profile uses the normal Zero 3W and Zero 4 DTBs. Its automatic boot selection is qualified per board.

Prepare the [native package update layer](../../../ember/image/README.md#package-updates-on-a-running-system) for aarch64.
Use an SDK client with `fetch-os-update` in that layer. Merge the site's network configuration and required device firmware into the layer.
Pass it as `-c`. A new installation needs its normal network and SSH settings before it can reach the release channel.

Supply the selected source commit and the original etc archive for that installation.
Take its SHA-256 from the authenticated SDK release record. This archive supplies the defaults for later three-way configuration merges.
Use the host Python from the SDK:

```sh
export A733_UPDATE_PYTHON="$HOST_PREFIX/bin/python3.15"
export A733_UPDATE_OS_COMMIT="$OS_COMMIT"
export A733_UPDATE_ETC="$SIGNED_ETC_ARCHIVE"
export A733_UPDATE_ETC_SHA256="$SIGNED_ETC_SHA256"
sh distrib/utils/embedded/mkimage -h a733-update -S "$OS_SOURCE" \
    -D "$DESTDIR" -K "$KERNEL_DIRECTORY" -c "$NATIVE_LAYER" "$IMAGE"
```

The builder creates equal FFS wedges named `netbsd-root` and `ember-root-b` after the EFI partition.
A holds the initial system with space for a download. B stays empty until preparation of the first trial.
The profile disables automatic resizing of the GPT and root. Supply `-s MIB` to reserve a larger whole image.
The builder rejects a size below the two-slot minimum.
It replaces an existing output only after the complete image is ready.

When a release changes a default file's contents, the slot installer keeps stricter local group and public read permissions.
The installed contents must match the old or new default. The release must keep the file's mode, owner and group. A release hard link with a local mode change holds the update.

The stock health check validates the installed SSH configuration and host keys, the running SSH service and a local default route.
Set `A733_UPDATE_HEALTH` to an executable for the image's own application to replace this check.
It must return zero only when the application can use its saved configuration and data.
The check runs locally and does not contact S3.
Read the [installed OS service contract](../../../ember/tools/docs/automatic-system-update.md) for confirmation and rollback.

### Enable an already provisioned card

Use this operation only on an installed slot A with the same dedicated A, B and EFI wedges.
Keep its normal `boot.scr`. The live operation checks the active model, layout, kernel and SDK.
It checks the baseline archive against the digest from the verified release before writing new files.
It preserves existing site files outside the listed profile configuration.
Supply the OS source tree containing the two-slot support on the device, then run as root:

```sh
/usr/pkg/bin/python3.15 -I "$OS_SOURCE/ember/image/provision-a733-update.py" \
    --live --os-commit "$OS_COMMIT" --etc-archive "$SIGNED_ETC_ARCHIVE" \
    --etc-sha256 "$SIGNED_ETC_SHA256" --mkubootimage /usr/bin/mkubootimage
```

The bootstrap writes the current generation as zero, installs the one-shot loader, then enables `ember_os_update` for subsequent boots.
Use `--health /path/to/check` to install a site-specific probe. Run `/etc/rc.d/ember_os_update start` to start the first pass now.
An in-progress installation or another boot entry must be resolved before provisioning this profile.
