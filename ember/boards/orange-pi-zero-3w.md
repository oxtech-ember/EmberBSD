# Orange Pi Zero 3W

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

The capability table preserves earlier published hardware results. The
current-kernel check below records a separate physical run.
Board revision: not recorded in the original support table.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | Allwinner A733 |
| Boot path | Vendor boot0 and U-Boot, device tree; the boot script tells the board from the Zero 4 |
| Boot storage | microSD: Tested, SDR104 at 150 MHz (47 MB/s read) |
| Serial console | Tested |
| All CPU cores | Tested (8 cores, a minute of full load at 62 degC with the kit's cooler) |
| Ethernet | No port |
| Wi-Fi | Tested: WPA2 on 2.4 and 5 GHz with 802.11n, 32 MiB transfers each way with matching checksums; WPA3-SAE/H2E on 5 GHz with required PMF and 8 MiB each way also passes; needs vendor firmware; see Wi-Fi limits below |
| Bluetooth | Classic: inquiry Tested, pairing not tried; BLE: No; needs the vendor patch files |
| Temperature sensor | Tested (five sensors) |
| Fan control | Native PWM and thermal policy with software contracts; physical speed control remains unverified ([guide](../boot/a733-fan.md)) |
| Full power-off | `shutdown -p now` stops the stock kit fan through the firmware power-off path; physically confirmed with `EMBER64 #4`. Plain `halt` keeps power applied. PWM speed control has separate software checks; physical acceptance is pending. |
| Watchdog | Tested: resets the board |
| Power button | No |
| I2C | Tested (power management chip, Type-C controller) |
| Audio | No |
| Pin multiplexing and GPIO | Pin multiplexing: Tested; no pin interrupts |
| Real-time clock | Tested across a reboot |
| Processor frequency control | Tested: both clusters switch between 408 MHz and 1.8 or 2.0 GHz |
| Voltage regulators | All outputs of the AXP8191 read at boot; not measured |
| USB | Controllers and the Type-C controller attach; no device tried |
| Hardware random numbers | Tested |
| GPU/NPU power domains | Native PCK600 provider; GPU_CORE ON/Q readiness passes with the dedicated GPU PLL. Command execution has separate acceptance ([guide](../boot/a733-power-domains.md)) |
| GPU/NPU clocks and resets | Native main CCU providers; the opt-in GPU PLL cold start locks and confirms 600/200 MHz on this board. Normal boot preserves firmware state ([guide](../boot/a733-gpu-identification.md#physical-pll-result)) |
| GPU identification | Two explicit runtime runs read BVNC `36.56.104.183` after PLL600 and strict CORE ON/Q readiness ([physical result](../boot/a733-gpu-identification.md#physical-pll-result)) |
| GPU firmware | Native `firmload`, DMA preparation and firmware bootstrap pass. The firmware reports startup and matching hardware/firmware BVNC through direct coherent memory. LOGTYPE and MMUCACHE commands complete with matching return words, queue progress and executed counters. Later tracing and KICK reach firmware, but the compute context resets with page fault `0x2400`; the timeline stays zero, and the new DMA snapshot confirms an unchanged output word. Compute completion, rendering and accelerated Plasma remain unverified ([compute check](../boot/a733-gpu-fwcompute.md#physical-stages-19-21-result), [bootstrap](../boot/a733-gpu-fwboot.md#physical-bootstrap-result), [command](../boot/a733-gpu-fwprobe.md#physical-command-result)) |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Hardware notes

Use `shutdown -p now` for an orderly shutdown followed by firmware power-off.
The plain `halt` command stops the OS without requesting power removal, so
the stock fan can keep spinning. On 2026-10-08, the user confirmed that the
stock kit fan stopped after `shutdown -p now` on `EMBER64 #4` (`4125fa28057`).
After the user powered the board on again, eight CPUs, microSD root and SSH
returned. This verifies that shutdown scenario, not PWM fan speed control.

The Zero 4's processor, power management chip, wireless module and Type-C data port
on a smaller board without an Ethernet PHY; its tree is the Zero 4 one with the MAC
disabled. The same card starts both boards.

Tested on physical hardware: boot from microSD in SDR104, eight cores, five
temperature sensors, watchdog reset, frequency switching on both clusters, Wi-Fi on
2.4 and 5 GHz, Bluetooth inquiry, the real-time clock across a reboot. Not tried: USB
devices, Bluetooth pairing, a long run

## Wi-Fi limits and connected-scan regression

The 2026-10-08 check used a 4 GiB Zero 3W, AIC8800D80 SDIO firmware
0x06090101 (`g586bc1e8`, built 2025-12-05), and wpa_supplicant 2.11.
The GCC16 `EMBER64 #9` bundle from `060cf95ccc75b47a6c225425214d40596f672a3e`
booted with eight CPUs, microSD root and Wi-Fi. WPA3-Personal completed on a
5 GHz channel-60 mesh AP: SAE group 19, H2E, required PMF and BIP-CMAC-128.
An 8 MiB file transferred each way with an exact comparison. The persistent
5 GHz SAE profile uses the normal rc service and leaves BSSID selection open.
A normal reboot restores SAE/H2E, required PMF, mDNS/SSH and all eight CPUs.
On the preceding `c8fabb0f4` bundle, three reconnects
to the same AP without flushing PMKSA took 9, 10 and 9 seconds. A wrong
password caused three SAE attempts without connection over 30 seconds;
restoring the password, a WPA2 regression and the return to SAE passed. All 16 live
SAE ioctl checks passed. See [configuration, protocol and checks](../boot/aicwf-sae.md).

The earlier connected-scan failure is fixed: a user scan forced net80211
into INIT without notifying the supplicant that the old station had left.
The firmware disconnected, while the client retained its COMPLETED state.
The corrected path emits the missing departure before scanning. Three
`wpa_cli scan` and three `ifconfig aicwf0 list scan` trials recovered
association and traffic automatically; repeated trials took 11–30 seconds
to a successful gateway check. One required a retry after AP refusal.
On the final bundle, three SAE/PMF scans recovered 5 GHz traffic in 9, 11
and 9 seconds across two mesh APs. Host-side SSH and an exact 8 MiB transfer
passed after the series. These scans interrupt traffic; seamless scanning
is not claimed. The driver also respects beacon DS/HT primary channels
and the D80's eight-bit SDIO buffer count; both have causal regressions.

A forced 2.4 GHz SAE association completed, but a subsequent file transfer
stopped after about a minute. The AP log recorded band-steering removal at
the same time; a WPA2 control reproduced that policy. The firmware/client
retained COMPLETED before the SA Query fix described below. This trial does not establish
sustained WPA3 traffic on 2.4 GHz. The installed network profile restricts
candidates to 5 GHz to match the AP policy, without pinning one BSSID.
Other profiles retain their existing band choices. See
[band steering and acceptance](../boot/aicwf-sae.md#band-steering-and-acceptance).

`ifconfig aicwf0 list scan` actively scans. Use cached `wpa_cli scan_results`
to inspect results without starting another scan. On a board with Wi-Fi as
its only link, arrange serial access or a tested hardware-watchdog recovery
before changing the driver or authentication policy. The controlled baseline
kept running after the failed scan; no kernel panic was established.

The firmware key interface has no initial IGTK packet-number field.
Nonzero initial IPNs are rejected; networks or rekeys requiring them are
not supported yet. Protected-management replay/forgery injection and a
long-duration run remain unverified. This AIC path does not implement
802.11r FT, 802.11ax or host AP mode. The new
[host-managed k/v transport](../boot/aicwf-kv.md) has software regressions and
physical connected-scan, ioctl and management TX/RX checks. The AP returned
an empty neighbor response. Neighbor-list and BTM acceptance remain pending.
VHT80 with one stream is physically checked under WPA2-CCMP. TCP and 8 MiB
file transfers pass in both directions; see the [current results](../boot/aicwf-vht.md#physical-vht80-check).
On an AP with a nonzero initial IGTK IPN, SAE/H2E succeeds but key installation
fails. VHT80 with SAE/PMF remains unaccepted.
The [VHT80 and roaming audit](../boot/aicwf-vht.md) separates radio capabilities from driver support.
CM5 bwfm results do not validate these features on AIC8800D80.

Bundle SHA256: ELF
`48080e3123b837b3afcc977602116ecd2b1f806fd727a832b029f94e2d20e3ff`;
native image
`3149a3e78f8db19643a2bcbb0bda572f10f35132bc3a398eba2c61d8e8335267`.
The four modules were installed from the same build. Existing DTB, boot
firmware and partition layout were preserved; a previous kernel remains
available for recovery.

## Protected association recovery, 2026-10-09

Cross-built `EMBER64 #11` from `aaff1e121ce4a77748ff6b7be21f4e226c9e3c4b`
booted on the same 4 GiB board with four matching modules and the updated
base wpa_supplicant 2.11. A positive probe sent a protected SA Query and
received the peer's protected response with the matching transaction ID.
The driver admitted its firmware CCMP/key status and fresh packet number.

The previous diagnostic build had received unprotected deauthentication
frames after AP removal without passing them to the supplicant. The new
path reported reason 6, let the protected SA Query time out, and began a
new association. In the accepted trial, the client moved from 2.4 GHz to
a 5 GHz channel-60 mesh AP automatically. Association recovered about
27 seconds after the event, after two authentication timeouts; gateway
traffic returned in about 40 seconds. A separate host then verified SSH
and an exact 8 MiB transfer each way before acknowledging the watchdog.
The test's fallback service restart did not run. This is recovery after
an interruption, not seamless roaming or 802.11r/v support.

A first trial also reconnected automatically, but its file check overlapped
the test fallback; it was not used as traffic acceptance. The router refused
a later forced join to the same 2.4 GHz AP. The accepted trial used the
other mesh AP. Router policy and persistent credentials were unchanged.
The ordinary 5 GHz profile remains active; diagnostic logging is disabled.
See [the protocol and recovery boundary](../boot/aicwf-sae.md#band-steering-and-acceptance).
Firmware integrity enforcement still needs RF forgery/replay testing.

SHA256:

| Component | Hash |
|---|---|
| ELF kernel | `bfd0d09f63dc364570a6d0f4b59879964f26d456016634e8053048e15095939b` |
| Native image | `1fd60e47965f2eec95e0fabeb01de63f0e44c93393c1c965ef7264dc483c5696` |
| wpa_supplicant | `0e5235f96fee5830b1ba4a1662c6b5f548e5489ab9b8bede99a1144db954271c` |

## Evidence

### Accelerator provider boot, 2026-10-08

The matched GCC16 `EMBER64 #5` bundle from `f625fd9a0de` boots on this
4 GiB Zero 3W with eight CPUs, microSD root and Wi-Fi/SSH. The read-only
GPU consumer observes stable CCU snapshots with the GPU module/bus clocks
gated and reset asserted. DCDC4 is programmed to 800 mV and GPU_TOP is
statically ON. The probe stops before GPU MMIO. The [physical result and bundle hashes](../boot/a733-gpu-identification.md#physical-result)
distinguish this outcome from the passing software contracts. No active
accelerator power sequence, DMA, firmware load or command execution was tested.

### Current kernel check, 2026-10-07

A physical Zero 3W with 4 GiB RAM booted `EMBER64 #0` from commit
`aed986038b1da44cbc9cb8b124b25ec58587bd11`. The board revision was not
recorded. Native GCC 12.5 built the kernel and three matching modules from
a clean export in 55 minutes 54 seconds. The highest sampled temperature
was 57.1 degC. The existing NetBSD Python build contracts passed separately
in an AArch64 VM; the board did not require a Python installation.

After the update, all eight cores, microSD root and Wi-Fi returned. A 32 MiB
file transferred in each direction with matching SHA256. All 16 memfd ATF
cases passed using the same test binary that reproduced two partial-page
mapping/seal failures on the previous kernel. FP defaults and preservation
across threads, signals, fork and exec also passed. Those FP cases already
passed on this board before the update; this does not reproduce the
initial-state bug seen on CPUs without AArch32.

The native image SHA256 is
`5f5c9bd1abef2dda00606ad42d0dce4de5beb58868994bdfd473adc181b351ef`;
the ELF kernel SHA256 is
`aaaa0443d4e63311091267ffcc8e69a941757b92962720296468936cc695f600`.
Both newly built A733 DTBs matched the installed files byte for byte.
Vendor boot files, firmware, storage layout and network settings were
preserved. The old kernel and module tree remain available for rollback;
the active module directory contains the three modules from this build.

The NetBSD 11 userland now includes a complete rebuilt shared/static libc
from `cffffd40`, with the [outlined CAS](../boot/aarch64-outlined-cas.md) and
[binary128 comparison](../tools/aarch64-binary128.md) fixes. Native CAS 850,
binary128 3,600-row/16-mode checks and 27 existing libc/thread cases pass;
hardware IOE is unavailable, so trap cases skip. After installation, fresh
processes pass smoke, CAS and the masked matrix through the default loader.
The previous libraries are retained for rollback; other userland and headers
were not rebuilt as a complete release.

These short boot, network and regression checks do not establish a full release,
sustained uptime,
hardware GPU/NPU execution or a physical Wayland session. Accelerators and
the newer common development packages still need their own acceptance.

### Earlier support matrix

Results were migrated from the [published support matrix](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
