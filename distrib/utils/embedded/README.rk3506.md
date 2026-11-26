# Build a Luckfox Lyra Zero W SD image

Use the embedded image builder with the common earmv7hf `GENERIC` outputs. The first bring-up image has one FAT partition and starts the kernel from U-Boot. The full layout also places a matching EmberBSD userland in an FFS partition named `netbsd-root`.

## Inputs

- Set `src` to the OS source tree. Set `kernels` to the kernel output directory containing `netbsd-GENERIC.ub` and `rk3506b-luckfox-lyra-zero-w.dtb`.
- Obtain `u-boot-rockchip.bin` from the board-specific Armbian package. Set `RK3506_UBOOT` to its absolute path. The builder checks the SHA-256 of the loader before placing it at LBA 64.
- Set `tooldir` to the OS host tools directory and `host_prefix` to the native SDK prefix. Use `HOST_SH` from the selected native tools.
- For a full image, add the matching ELF kernel `netbsd-GENERIC` to the kernel directory. Supply an earmv7hf DESTDIR with its base and etc manifests. A private root overlay can supply the site configuration.

The board profile identifies the loader package, versions, hardware applicability, boot order and recovery limits. [DevBoards boot profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/rk3506b-lyra-zero-w/boot.md)

## Build

```sh
export TOOL_MKUBOOTIMAGE="$tooldir/bin/nbmkubootimage"
export TOOL_MAKEFS="$tooldir/bin/nbmakefs"
export TOOL_GPT="$tooldir/bin/nbgpt"
export TOOL_CKSUM="$tooldir/bin/nbcksum"
export TOOL_MTREE="$tooldir/bin/nbmtree"
export TOOL_OBJCOPY="$host_prefix/bin/llvm-objcopy"
export TOOL_PYTHON="$host_prefix/bin/python3.15"
export TOOL_FDTGET="$host_prefix/bin/fdtget"
export RK3506_UBOOT

# Prepare the console image. makefs reads the user and group files from the OS tree.
"$HOST_SH" "$src/distrib/utils/embedded/mkimage" -h luckfox-lyra-zero-w \
    -b -S "$src" -K "$kernels" "$output/lyra-console.img"

# Build the full layout when the matching target userland is available.
"$HOST_SH" "$src/distrib/utils/embedded/mkimage" -h luckfox-lyra-zero-w \
    -S "$src" -D "$destdir" -K "$kernels" "$output/lyra-system.img"
```

For a prepared static root, give the builder the root tree and its matching BSD mtree specification.
The tree supplies the userland and startup configuration. The specification supplies the target modes, owners and device nodes.
The static network profile exports this pair as `root/` and `root.spec`. The card builder creates the FFS partition directly.
Apply the private configuration in the static root producer. Select the root device for this card when preparing a multi-user tree. The GPT label is `netbsd-root`.

```sh
"$HOST_SH" "$src/distrib/utils/embedded/mkimage" -h luckfox-lyra-zero-w \
    -S "$src" -D "$static_output/root" -F "$static_output/root.spec" \
    -K "$kernels" "$output/lyra-system.img"
```

Use a regular output file. The builder prepares a new file before replacing an existing image. The FAT partition starts at 16 MiB. U-Boot reads `boot.scr` there and passes the external DTB to the ARM kernel. The script places the legacy image header at 0x02000000. The first instruction then runs at 0x02000040, as required by the evbarm initial section mapping.

Inspect both GPT copies, read the loader back at byte 32768 and extract the kernel, DTB and boot script from FAT. Compare the bytes with the selected inputs. Physical acceptance requires a cold boot on the target, console output from the selected kernel, and, for a full installation, a mounted root and a network login.

Test the kernel header and CRC checks, init link resolution and the console device with the selected host tools.

```sh
"$TOOL_PYTHON" "$src/distrib/utils/embedded/check-armv7-uboot-test.py" "$TOOL_MKUBOOTIMAGE"
"$TOOL_PYTHON" -B "$src/distrib/utils/embedded/check-prepared-root-test.py"
```
