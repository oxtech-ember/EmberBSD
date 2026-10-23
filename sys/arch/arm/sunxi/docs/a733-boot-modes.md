# A733 native and diagnostic DTB modes

The normal Zero 3W DTB starts GPU firmware and KMS after the root mount.
GPU startup completes before init(8). Single-user mode cannot prevent it.
Select a diagnostic DTB before kernel entry when automatic startup is unwanted.

`sun60i-a733-orangepi-zero3w-diagnostic.dts` includes the normal Zero 3W tree.
It adds the empty `netbsd,observe-only` property to the GPU and display nodes.
It changes no hardware description. It uses the same kernel and drivers.
The GPU keeps its explicit stage controls without automatic firmware startup.
The KMS frontend keeps its explicit controls without automatic output startup.
The diagnostic mode does not promise zero writes by every system driver.
NPU mode is unchanged. Zero 4 keeps its existing GPU mode and disabled display.

See the [GPU binding](a733-gpu-identification.md) and
[KMS startup contract](a733-drm-startup.md) for each owner's behavior.
Loader applicability and recovery procedures belong to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/boot.md).

## Native build target

`ember/build-kernel.sh` exports the normal and diagnostic DTBs together.
An existing configured kernel object directory can build only the DTBs.
Use the matching GCC16 build wrapper and its source tree:

```sh
"$nbmake" -C "$kernel_obj" DTSGNUARCH=arm64 DTSSUBDIR=allwinner \
    sun60i-a733-orangepi-zero3w.dtb \
    sun60i-a733-orangepi-zero3w-diagnostic.dtb \
    sun60i-a733-orangepi-zero4.dtb
python3 ember/tools/a733-boot-modes-contract.py "$kernel_obj"
```

The native suffix rule is in `sys/conf/dts.mk`. It does not rebuild the kernel.
Record kernel, module and DTB source revisions separately when reusing a bundle.
Verify the unchanged input interfaces. A new DTB does not update an old kernel.

## Boot script input

`ember/boot/orangepi-zero4-boot.cmd` consumes `ember_boot_mode`.
An unset value or `normal` selects the existing normal board DTB.
`diagnostic` selects the separate diagnostic DTB after Zero 3W detection.
An unknown mode or a diagnostic request on Zero 4 stops before file loading.
A failed DTB load, FDT operation or kernel load stops before `booti`.
The existing `devtype`, `devnum`, `distro_bootpart` and `prefix` select storage.
Both modes use the same native kernel image and the same shared script.

Each independent loader entry must set its mode before it sources `boot.scr`.
The normal entry must set `normal`; it must not inherit a diagnostic selection.
A script variable alone does not create an accessible or persistent entry.
A failed normal boot does not automatically select diagnostics.
Verify the independent entry through the actual loader before relying on it.

## Checks and limits

The contract reads the three compiled DTBs. It requires the complete diagnostic
property map to match normal mode except for the two empty mode properties.
It also checks normal and Zero 4 startup policy and the Zero 3W identity.
The actual boot script runs with modeled U-Boot command boundaries.
Controls cover modes, board decisions, storage prefixes and failed loads.
These checks do not execute U-Boot, confirm GPIO board detection or qualify
physical recovery. Independent loader entry and hardware checks remain required.
