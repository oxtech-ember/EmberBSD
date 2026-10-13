# Complete EMBERGPU cross build and isolated boot

On 2026-10-07, clean revision `c6aba7d1e2400cd1ec43b2a9d96905be86c346d1`
cross-built on Apple Silicon macOS using `ember/build-kernel.sh`, `EMBERGPU`,
six jobs and the in-tree GCC12.5 bootstrap. Both kernel formats, four matching
board modules and three DTBs were produced. Portable contracts passed on
the host; all six wrapper contracts passed in AArch64 NetBSD 11 with GCC16.
The module/DTB builds do not establish attachment on their physical boards.

The image booted in QEMU 11.1.2 `virt` with HVF, two CPUs, 1 GiB RAM, a
temporary 64 MiB FFS root, PCI VirtIO block/GPU devices, serial console,
no display frontend and no network. The root used NetBSD 11 rescue utilities
and the tested AArch64 loader/libc/libpci. It did not boot a full current
userland, the existing UTM desktop or its disks.

```sh
qemu-system-aarch64 -machine virt,accel=hvf -cpu host -m 1024 -smp 2 \
  -display none -serial stdio -no-reboot -kernel netbsd-EMBERGPU.img \
  -append 'root=ld4a console=plcom0' \
  -drive file=root.ffs,if=none,format=raw,id=root \
  -device virtio-blk-pci,drive=root -device virtio-gpu-pci \
  -net none -monitor none
```

`ld4` follows this kernel configuration; do not assume `ld0`. The root was
made with the same build's `nbmakefs`, using the target device specification
from `MAKEDEV -s std ld4 drm0 ttyE0`. It contains `/rescue`, `/bin` and `/sbin`
rescue links, the ELF loader at `/usr/libexec/ld.elf_so`, target DSO SONAMEs
and the probes below. Its `/etc/rc` records statuses and calls `halt -p`.

## Observed results

VirtGPU attached as `virtiodrm0` at PCI `0000:00:02.0`, vendor/device
`1af4:1050`, with one scanout, no VirGL, no EDID and no capsets. The root
mounted, all probes ran, filesystems unmounted and QEMU exited with status 0.

The complete [Ports libdrm 2.4.134nb1 cross payload](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-graphics/cross)
passed upstream hash, skip-list and device enumeration. Enumeration returned
the same native PCI metadata through both primary and render nodes; this was
an actual device check, not a missing-device skip. The
[Examples memory probe](https://github.com/oxtech-ember/EmberBSD-Examples/blob/ea73dffa697535db86b0a60b4ad9afeeb02b293c/desktop/wayland-utm/drm-memory.c),
cross-built with GCC16, passed malformed requests and all 32 cross-process
GEM/PRIME mapping-lifetime cycles against that libdrm.

This direct serial boot has no firmware simplefb console to reserve.
`console unavailable: -19` records an excluded console handoff, not a passed
visible-console check. A PMU initialization warning is outside these graphics
checks. Visible KMS, VT/exit recovery, UTM, host DMA qualification, accelerated
rendering and a sustained session remain unverified for this kernel.

## Artifact identities

| Artifact | SHA256 |
|---|---|
| netbsd-EMBERGPU | `bb5d53a2c6e503322f175f42cf0d4049ae92d95c47351ec7b400f5852c20c847` |
| netbsd-EMBERGPU.img | `b029a0cc5eb685a610e57b9ac4bc6d1267bbdeda9124a1d5a82d5b2349784565` |
| libdrm.so.2.134.0 | `69c97678919c929a5f71ab4c5f2bcb4d922c267c1255079dae09b65bfca23bfc` |
| drm-memory | `15e88792f7008fe16bbb76259ae7e08393b1df643ca69ca5d6c35c9f7326e4e4` |

Logs and the temporary root remain outside Git. NetBSD, QEMU and libdrm keep
their authorship and licenses. This receipt does not enable VirGL or claim
upstream acceptance of EmberBSD adaptations.
