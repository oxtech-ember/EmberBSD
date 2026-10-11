# Compute Module 5 (D0)

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

The 2026-10-08 hardware check used Raspberry Pi Compute Module 5 Rev 1.0,
BCM2712 D0, 4 GiB RAM and nominal 64 GB eMMC on Waveshare CM5-NANO-B.
The carrier PCB revision and exact four-wire fan model were not established.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | BCM2712 D0 |
| Boot path | UEFI and ACPI; normal boot and warm reboot tested |
| Boot storage | eMMC boot, installation readback and 8 MiB file round trip tested |
| Serial console | Previously published as tested; not repeated in this check |
| All CPU cores | Four cores online; 850 atomic compare-and-swap cases passed |
| Ethernet | Gigabit link and SSH/file transfer tested; see lifecycle limitation below |
| Wi-Fi | WPA2-PSK and WPA3-Personal SAE group 19/H2E with required PMF tested; silent firmware disconnect recovered automatically, followed by an 8 MiB Wi-Fi round trip ([receipt](../boot/bwfm-link-events.md#physical-cm5-check)) |
| Bluetooth | Not validated |
| Temperature sensor | ACPI and VideoCore readings checked; no undervoltage/throttle flags during the short check |
| Fan control | Persistent maximum cooling and reboot tested; low/intermediate PWM cycles on this fan ([control](../boot/thermal-fan.md)) |
| Watchdog | Not validated |
| Power button | BCM2712 GPIO20 fallback loads; physical PSW shutdown/power-on pending ([setup](../../sys/modules/rpi5button/README.md)) |
| I2C | Not validated |
| Audio | Not validated |
| Pin multiplexing and GPIO | Not validated |
| Real-time clock | Not validated |
| Processor frequency control | Not validated |
| Voltage regulators | Not validated |
| USB | Not validated as a running OS peripheral; USB boot/eMMC provisioning is separate |
| Hardware random numbers | Not validated |
| V3D GPU | Opt-in physical experiments completed reset, translated TFU DMA, bin/render, IRQ delivery and one fault/recovery cycle ([checks](../boot/bcm2712-v3d-validation.md)). The [native DRM/GEM path](../boot/bcm2712-v3d-render.md) passed 420 userspace TFU copies and mmap/lifetime checks. The [native CSD path](../boot/bcm2712-v3d-csd.md) passed 389 QPU dispatches with verified integer writes, guards, concurrent clients and TFU transitions on 2026-10-11. A later [binary syncobj check](../boot/bcm2712-v3d-sync.md) passed finite waits, 325 CSD submissions and 64 CSD/TFU chains. The [native CL check](../boot/bcm2712-v3d-gl.md) passed 325 bin/render pairs and 1,331,200 verified pixels. General job recovery, Mesa, Vulkan and an accelerated desktop remain unaccepted |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Hardware check 2026-10-08

The tested kernel is `EMBER64 #10`, built with GCC 16.2.0 for
`aarch64--netbsd` from commit
[`df809fb0d3843130dd1b0f08fb8610bb54261581`](https://github.com/oxtech-ember/EmberBSD/commit/df809fb0d3843130dd1b0f08fb8610bb54261581).
Cross-builds ran on macOS; execution and acceptance ran on the physical CM5.
The source check compared 35,620 kernel/common/build/client files against that
commit with no mismatches. The subsequent client guard change
[`5f8ab2a5`](https://github.com/oxtech-ember/EmberBSD/commit/5f8ab2a5)
produced a byte-identical wpa_supplicant binary and also cross-built hostapd.
Later changes merged into main are not automatically covered by this receipt.

| Installed artifact | SHA256 |
| --- | --- |
| `/netbsd` and `/boot/netbsd` | `9ca9f513fe49c353c1788b49d1aa94d66ed543b259e7e8b79be4f7bf0da7e3c5` |
| `/boot/netbsd.img` | `3f98ab14500ae1949a5943f12e1047d846565ef4f07d4dd3772a35e506ce2ad2` |
| wpa_supplicant 2.11 | `7ddc2f32c0be332d5d59f45550dcac75c57e614163f5363998ed8fc785e80f04` |
| bcm2712btcom.kmod | `b782474524f8cde244143503e09ba71a05a36414b3be75a7190a68b225de78f0` |
| if_cemac_acpi.kmod | `2f88ec1cf96e9d6bf1a02a8b434338a1ae525e3648b3db7d52343fd2a0b9fb72` |
| rp1wmcodec.kmod | `9c35c396116678f3c8ec9c9f57d8563887c20202e7ef8e50c53e8b2fc3a86a54` |
| rpi5button.kmod | `5747a4c1b71420a765a59366991ba320161784cbd53c827838512e862c08fb1b` |

The four board modules were built with the matching source and kernel
configuration. Only the Ethernet module was loaded for this acceptance;
installing the other modules is not evidence for their peripherals.
The existing UEFI was retained, not rebuilt from current main. Its exact source
revision is unknown. The `RPI_EFI.fd` snapshot after the final reboot at
13:20 UTC had SHA256 `d94ce975090358c38938e9c16037d2eecc5fffc29828029ff2d6460a189ed883`;
its whole-file hash changes across boots, so this is a snapshot identifier,
not a reproducible firmware build identity.

The radio was CYW43455, firmware 7.45.265 (FWID 01-b677b91b), on a 5 GHz
channel-52 access point offering WPA2/WPA3. An SAE-only client profile required
PMF. Firmware capability reporting was checked independently from connection
success. See [configuration, implementation and checks](../boot/bwfm-sae.md).

- 106 parser/security-boundary assertions passed on the host with ASan/UBSan
  and natively on CM5; 13 live ioctl checks passed on CM5.
- SAE group 19/H2E completed with CCMP/BIP. After traffic-key installation,
  the firmware reported `wsec=0x604`, `wpa_auth=0x40000`, `mfp=2`.
- An 8 MiB file was uploaded and downloaded with the peer route forced through
  bwfm0; byte comparison and SHA256 matched. This was repeated on final #10.
- Three disconnect/reconnect cycles completed in approximately 14 seconds
  each, with zero firmware control timeouts after command serialization.
- An incorrect password failed SAE authentication (status 15) during a
  35-second observation. It never connected or fell back to WPA2.
- The separate WPA2-PSK profile connected with CCMP and firmware `mfp=0`.
  Restoring the SAE profile re-established required PMF.
- After restoring the permanent profiles and rebooting, SAE/PMF and both
  network access paths returned automatically. Installed hashes were checked.

These are functional checks, not a throughput comparison, deauthentication
attack test, long soak or validation of another board/access point.
During an earlier test that administratively lowered cemac0, the board restarted
unexpectedly. No crash dump established the cause. Normal gigabit traffic is
verified; repeated Ethernet down/up and its interaction with Wi-Fi are not.

## Cooling

The carrier's [official schematic](https://files.waveshare.com/wiki/CM5-NANO-B/CM5-NANO-B-Sch.pdf)
shows switched 5 V power, PWM and tachometer on its four-pin fan connector.
The installed fan's exact model is unknown. The operator observed repeated
stops at low and 175/255 PWM, and continuous rotation at maximum 250/255.
Tachometer edges in an earlier short probe did not establish steady rotation.

The [maximum-cooling policy](../boot/thermal-fan.md) uses the existing ACPI
`_AL0` devices and survives a reboot when saved in sysctl.conf. It preserves
temperature monitoring. It is an explicit operating policy for this fan;
it does not establish working automatic speed control. No UEFI update or
raw MMIO utility is required. The existing UEFI remains installed.

## Home mesh and cooling check, 2026-10-08

The physical board booted GCC 16.2.0 `EMBER64 #3`, built on macOS from
`b78f10c721f7c397ef7b67e3aa6ec8fe65210d7e`. `kern.buildinfo` reports that revision.
The kernel, four matching modules and fixed wpa_supplicant came from that
source. The client and hostapd cross-builds passed using the updated headers.
The optional button rc service is from `ad44f1803`.

| Updated artifact | SHA256 |
| --- | --- |
| `/netbsd` and `/boot/netbsd` | `7b4603f6fdcae29ba6d7819bbd84bdd5bb774107801a6b56bc48e0fbc83864e5` |
| `/boot/netbsd.img` | `1dde929bd282404a828c018409827b8a0a23f1e5b29cff5ccfd0ad5af7bcd2c6` |
| wpa_supplicant 2.11 | `fe1e7dd5dfde52dce4f587e2448ef1d70e627867fd13bc030b992105da83c0dd` |
| rpi5button.kmod | `2960fbc1b1a4d7c9330096ff4aa0b0ef4f5679fdc7cd973a4f0b44f1c3698f3f` |

The other three module hashes match the earlier receipt. Installed files
were verified after reboot; previous kernel/module sets remain available
for rollback. The existing UEFI was retained.

The CYW43455 firmware remains 7.45.265. A KeeneticOS 5.1.6 mesh advertising
WPA2/WPA3 allowed WPA2 and SAE group 19/H2E with required PMF on 5 GHz
channel 60 (80 MHz firmware chanspec `0xe23a`). Firmware reported
`wsec=0x604`, `wpa_auth=0x40000`, `mfp=2`, `roam_off=1` and `wnm=0`.
An 8 MiB upload/download passed byte comparison and SHA256 with the peer
route explicitly using bwfm0. This is a functional check, not a benchmark.
The fixed [external-SAE client](../boot/bwfm-sae.md#reconnecting-with-external-sae)
completed three reconnects to the same 5 GHz BSSID without clearing PMKSA
in 15, 14 and 14 seconds. A separate WPA2 profile connected in about
13 seconds; restoring SAE took about 13 seconds. An unpinned connection
also selected a stronger 2.4 GHz AP, so 5 GHz is available but not guaranteed
by a dual-band SSID. Autonomous firmware WNM/roaming remains disabled.

[Scan metadata](../boot/bwfm-scan.md) now preserves firmware primary channels
and exports signed RSSI as unsigned quality without wraparound. Its 166
checks and the thermal-notification contract passed on macOS and natively
in an EmberBSD AArch64 VM; six existing native kernel contracts also passed.
The board already connected on 5 GHz before the metadata fix, so this check
does not assign all previous band-selection failures to that defect.

The maximum-cooling setting loaded automatically after reboot and the
operator confirmed continuous fan rotation. On the preceding `72c00a3bbc19`
build, 19 samples over 18 minutes retained D0 with temperatures 37.5–41.3 °C.
Invalid values -1 and 2 were rejected; changing the policy to 0 and back to 1 produced D3 and D0.
Suspend/resume, sustained load and automatic intermediate speeds remain
unvalidated. 802.11r/802.11v are not added by this update.

## Earlier evidence

The original eMMC/serial/Wi-Fi results were migrated from the
[published support matrix](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. That commit identifies a documentation snapshot, not every
historical test's kernel. Future checks should use the
[evidence format](adding-a-board.md#record-the-result).
