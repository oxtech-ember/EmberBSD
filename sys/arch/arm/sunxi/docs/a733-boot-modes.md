# A733 DTB modes

The normal Zero 3W DTB starts GPU firmware and KMS after the root mount.
GPU startup completes before init(8). Single-user mode cannot prevent it.
Select a diagnostic DTB before kernel entry when automatic startup is unwanted.

`sun60i-a733-orangepi-zero3w-diagnostic.dts` includes the normal Zero 3W tree.
It adds the empty `netbsd,observe-only` property to the GPU and display nodes.
It changes no hardware description. It uses the same kernel and drivers.
The GPU keeps its explicit stage controls without automatic firmware startup.
The KMS frontend keeps its explicit controls without automatic output startup.
The diagnostic mode does not promise zero writes by every system driver.
NPU mode is unchanged.

The normal Zero 4 DTB keeps the GPU in observe-only mode and leaves display
disabled. The optional Zero 4 graphics DTB includes the normal Zero 4 board
description and enables the native GPU and display paths shared with Zero 3W.
It retains the Zero 4 Ethernet, USB and pinmux configuration. It also
attaches the passive NPU with the same supply and operating point as the
NPU observe mode. Its empty `netbsd,init-on-demand` property permits
checked preparation on the first admitted tile ioctl. Attachment at boot
remains passive. NPU observe mode uses explicit stages.
GPU startup requires the PowerVR firmware on the root filesystem.
HDMI startup borrows DCDC2 at 800 mV and CLDO2 at 1800 mV from the platform
boot firmware. The HDMI provider checks the enabled rails without changing them. GPU and KMS
registration and a visible HDMI session require separate physical checks.

The board bindings come from the pinned Xunlong [Zero 4 tree][vendor-zero4]
and [Zero 3W tree][vendor-zero3w]. Both connect HDMI to AXP8191 DCDC2 and
CLDO2, select the external HDMI PHY resistor and connect GPU to DCDC4.
The [DietPi Zero 4 image][dietpi-zero4] with SHA-256
`9c407d18fd2e6b937656f76eff2eb4b84e86689b39107f00e2a7306d1b983eef`
selects `allwinner/sun60i-a733-orangepi-zero4.dtb` in `/boot/dietpiEnv.txt`.
Its selected DTB has SHA-256
`f77997299fa970009db20e45db4c3685b3758a30ff19f514d2a04f47908a689e`
and carries these same bindings. Board selection is explicit in the DietPi
boot configuration. Its vendor root compatible string remains
`xunlong,orangepi-4-pro`. The EmberBSD profile retains the native Zero 4
compatible string.

[vendor-zero4]: https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/arch/arm64/boot/dts/allwinner/sun60i-a733-orangepi-zero4.dts
[vendor-zero3w]: https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/arch/arm64/boot/dts/allwinner/sun60i-a733-orangepi-zero3w.dts
[dietpi-zero4]: https://dietpi.com/downloads/images/DietPi_OrangePiZero4-ARMv8-Trixie.img.xz

See the [GPU binding](a733-gpu-identification.md) and
[KMS startup contract](a733-drm-startup.md) for each owner's behavior.
Loader applicability and recovery procedures belong to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/boot.md).

## Zero 4 NPU observe mode

The optional Zero 4 NPU observe DTB enables the passive NPU node and
describes its supply and one operating point.
It keeps the GPU in observe-only mode and the display disabled. Ethernet,
Wi-Fi and storage retain the normal Zero 4 configuration. The passive
NPU attachment exposes diagnostics without starting the power or DMA stages.

The Zero 4 DietPi DTB cited above connects the NPU supply to AXP8191 DCDC2,
which also supplies HDMI. The donor table specifies 492 MHz at 800 mV across the listed silicon bins.
Verify rail state, power ownership and the clock rate through the driver
before any explicit NPU power stage.
The NPU observe mode has a physical acceptance record for the fixed INT8
tile profile. See [Zero 4 boot and recovery][zero4-boot].

[zero4-boot]: https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero4/boot.md

## Native build target

`ember/build-kernel.sh` exports the normal, diagnostic, graphics and NPU observe DTBs together.
An existing configured kernel object directory can build only the DTBs.
Use the matching selected-toolchain build wrapper and its source tree:

```sh
"$nbmake" -C "$kernel_obj" DTSGNUARCH=arm64 DTSSUBDIR=allwinner \
    sun60i-a733-orangepi-zero3w.dtb \
    sun60i-a733-orangepi-zero3w-diagnostic.dtb \
    sun60i-a733-orangepi-zero4.dtb sun60i-a733-orangepi-zero4-graphics.dtb \
    sun60i-a733-orangepi-zero4-npu-observe.dtb
python3 ember/tools/a733-boot-modes-contract.py "$kernel_obj"
```

The native suffix rule is in `sys/conf/dts.mk`. It does not rebuild the kernel.
Record kernel, module and DTB source revisions separately when reusing a bundle.
Verify the unchanged input interfaces. A new DTB does not update an old kernel.

## Boot script input

`ember/boot/orangepi-zero4-boot.cmd` consumes `ember_boot_mode`.
An unset value or `normal` selects the existing normal board DTB.
`diagnostic` selects the separate diagnostic DTB after Zero 3W detection.
`graphics` selects the separate graphics DTB after Zero 4 detection.
`npu-observe` selects the passive NPU DTB after Zero 4 detection.
An unknown mode or a mode for another board stops before file loading.
A failed DTB load, FDT operation or kernel load stops before `booti`.
The existing `devtype`, `devnum`, `distro_bootpart` and `prefix` select storage.
All modes use the same native kernel image and the same shared script.
To use the explicit graphics mode, the boot partition must contain the
compiled `boot.scr` from the updated source and
`dtb/allwinner/sun60i-a733-orangepi-zero4-graphics.dtb`.
The NPU observe mode requires the updated script and
`dtb/allwinner/sun60i-a733-orangepi-zero4-npu-observe.dtb`.

Each independent loader entry must set its mode before it sources `boot.scr`.
The normal entry must set `normal` explicitly.
A script variable alone does not create an accessible or persistent entry.
A failed normal boot does not automatically select diagnostics.
Verify the independent entry through the actual loader before relying on it.

## Checks and limits

The contract reads the five compiled DTBs. It requires the complete diagnostic
property map to match normal mode except for the two empty mode properties.
It checks the Zero 4 NPU supply and operating point, both board identities and the Zero 4 graphics bindings.
It compares the full Zero 4 NPU observe property set outside the graphics
changes.
For known reference bindings, it compares provider paths and literal arguments.
The actual boot script runs with modeled U-Boot command boundaries.
Controls cover modes, board decisions, storage prefixes and failed loads.
These checks do not execute U-Boot, confirm GPIO board detection or qualify
physical recovery. Independent loader entry and hardware checks remain required.
