# YS-M33 closed-enclosure cable installation

Origin: EmberBSD - physical installation receipt recorded by Codex (GPT-6).

Date: 2026-10-08. Second factory YS-M33 tablet, Allwinner A133, 1 GiB DDR4,
32 GB eMMC, landscape desk mount. Separate DC power and USB-C to the host;
Ethernet was connected for EmberBSD acceptance. No enclosure opening or UART
connection was used on this tablet.

## Observed procedure

- Factory Android 10 `a133-10.0-20231211.133302` offered root through vendor
  `/system/xbin/su 0` while flash was locked and verified boot was green.
- All 17 GPT partition bounds matched the inspected reference. Original
  environment, boot, recovery, resources and hardware boot areas were saved.
- A full 31,037,849,600-byte live Android snapshot passed independent size,
  decompression, SHA256, GPT CRC and separately saved partition-range checks.
  Recovery then supplied an unmounted 27,676,098,048-byte UDISK snapshot and
  a 16 MiB metadata snapshot. Backups and identities remain private.
- A temporary hook cleared itself and saved normal boot before entering signed
  USB recovery. Restoring the full original environment returned to Android
  with locked/green state. Recovery required `adb root` after enumeration.
- Vendor unlock persisted across a reset. Signed recovery then reported
  unlocked/orange state. Boot0, TOC1, BL31 and OP-TEE were retained.
- The 32 MiB boot image, 4 GiB FFSv2 root and 32 MiB resource partition were
  written and verified through complete device readback hashes.
- First boot armed recovery for the next reset. The 120-second acceptance
  guard requested a clean reboot; the filesystem was clean and signed USB
  recovery returned without UART. MCU keepalive maintained recovery access.
- The final root uses fresh operator/host keys and a private independent
  entropy seed. Its initial VM-derived seed had zero entropy credit and was
  replaced before acceptance. Native seed loading and physical
  `kern.entropy.needed=0` were verified. No key or seed is included here.
- Ethernet SSH, 100baseTX full duplex, Xorg and awesomeWM started. The normal
  loader environment was written and read back, the pending marker removed,
  and a subsequent clean restart booted EmberBSD and awesomeWM again.

## Installed artifacts

Kernel: `EMBER64_A133_AUDIO #5`, clean source `2db8e449fd25`, built
2026-10-08 09:48:47 UTC on NetBSD 11/aarch64.

| Artifact | SHA256 |
| --- | --- |
| Boot, 32 MiB | `a3a5dbc0aee395f2638a33632e7f4f190293080937a69072af3652ca1cf5491a` |
| Final FFS root, 4 GiB | `41344e6fe0da03f516d70d66c2c0553cfc17f331a2ffcba5ae90189856255b87` |
| Final resources, 32 MiB | `893a03ed16a3c856d1f9c03e7817631fff3f9b2b6afc7a6bf00398be83473413` |
| Landscape desktop PNG | `73f08bf6ecf957c205defe52454d68e0ec8da5f7b86890000dd4303872cb3889` |

The approved flame mascot replaced both `bootlogo.bmp` and
`orange_warning.bmp`; all 31 other resource files and the FAT boot sector were
preserved. Every file extent fits the actual 32 MiB GPT partition, despite
the vendor FAT header advertising a larger volume. Clockwise rotation produces
the 800x1280 panel bitmap; a reverse conversion matches the 1280x800 source
pixel for pixel. The live desktop screenshot showed the same composition.
Replacing warning artwork does not change the unlocked/orange security state.

## Limits

This is one factory-image installation with operator-controlled host tools,
not a published reusable fleet installer. Cold removal/restoration of power,
full Android rollback and recovery from a corrupt loader environment remain
untested. The second tablet's touch/audio devices attached, but physical touch,
speaker and microphone acceptance was performed only on the reference sample.
The private voice application was not cloned. Built-in Wi-Fi has no network
interface; radio initialization, firmware loading and association remain work.
