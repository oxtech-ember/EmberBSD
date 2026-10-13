# A133 audio driver: build and physical default-off attachment

Validation date: 2026-10-08. This receipt covers the experimental YS-M33
audio(4) driver, not physical playback/capture acceptance.

## Implemented boundary

`EMBER64_A133_AUDIO` includes the preceding eMMC configuration and adds the
guarded A133 codec adapter, its register/FIFO helper and PCM core. The FDT
marker requires the inspected vendor codec/machine/GPIO/interrupt resources.
Existing non-A133 codec drivers remain unchanged.

Attachment and read-only queries do not prepare hardware. Speaker and MIC1
routes default to disabled. Explicit enable prepares clocks and analog paths
under the thread lock. Streams validate formats and allocation bounds, use
bounded interrupt service, and clear callbacks under the interrupt lock on
halt. No sound or microphone recording runs automatically.

The helper never asserts codec bus reset. After releasing an initially held
reset, it deliberately leaves reset released when restoring original gates;
otherwise unowned BIAS, DAP and calibration state could be erased. A timeout
before release preserves the original reset state. This exception is part
of the resource contract, not a claim of complete CCU-state restoration.

## Checks actually run

- Eight production-source contracts passed on macOS and native NetBSD:
  `a133-fdt-test`, `a133-pin-test`, `gt9xx-frame-test`, `gt9xx-reset-test`,
  `a133-pcm-test`, `a133-audio-io-test`, `a133-audio-fdt-test` and
  `a133-audio-interface-test` in [ember/tools](../../tools).
- The I/O and adapter contracts also passed AddressSanitizer and
  UndefinedBehaviorSanitizer on macOS.
- Native NetBSD 11/aarch64 GCC 12.5 built the full `EMBER64_A133_AUDIO` kernel
  ELF and ARM64 Image from a clean source pin. Initial compilation caught
  an incorrect mono format constant and missing `kpause` declaration; both
  were corrected in the source before exporting another clean pin.
- The native PCM kernel-context gate passed with the actual kernel flags.
- A reset-aware regression reproduced the destructive-reset defect before
  its fix. Cold/warm leases now preserve seeded BIAS, DAP and calibration.
- The adapter fixture executes real trigger, halt, service and ISR bodies:
  finite blocks and wrap, signed capture words, flush timeout, impossible
  FIFO count, and no I/O/callback after halt. It is a software model.
- Independent read-only review found no remaining critical/important issues.
  `git diff --check` passed.

## Installed artifact and recovery

The ARM64 Image is 16,989,540 bytes. The private Android v2 boot container
is 17,002,496 bytes, with an empty ramdisk and the 8,351-byte DTB payload
retained from the preceding working container. Its original provenance is
not established; vendor U-Boot supplies the full inspected board tree at boot.
Header/version, load
addresses, page alignment, exact kernel/DTB payloads and the 32 MiB partition
size limit were checked. The container SHA-256 is
`dd5f70773680ea0072355deb6f8d4f32a1c596731822fddd16f28b0cf3b22aa0`.
The native build source is `811491a81414e8dcd720e13fa584ce1ccbc05d5e`;
the final reviewed driver sources in `d245249485d2` are identical.

UART on J1 was verified in both directions, and an automatic serial relay
intercepted the three-second U-Boot countdown following a software reboot.
The quiet running console was not evidence of a broken UART connection.
The kernel first booted from RAM. The downloaded container had to be copied
from `0x41000000` to `0x45000000` before `bootm`, to avoid source/destination
overlap during kernel relocation. Vendor U-Boot and its environment were retained.

Before writing, the complete 32 MiB boot wedge was backed up off-device and
its SHA-256 verified:
`195481669c9128594e9064e56f0adb76703ca73794bb861036413ed1ed8a079d`.
The inspected GPT boot wedge occupies 65,536 sectors at sector 172,032.
Only the container prefix was written. Complete partition readback matched
that prefix byte for byte, and its untouched suffix matched the backup.
The readback SHA-256 is
`cb2ce60aceaa4ef31d76ee4a25eafa9653357509bb9de55a8b6630a5d2a95b72`.
U-Boot's independent container CRC32 also matched `435b7361` before eMMC boot.

Physical `EMBER64_A133_AUDIO #0` boot identified `a133codec0` and `audio0`.
Read-only queries enumerated mono playback/capture and stereo playback at
16/48 kHz, with `outputs.route=disabled` and `record.route=disabled`.
Play/record open and active counts and sample counters were zero.
SSH, Ethernet, MCU keepalive, Xorg, awesomeWM and terminal/clock windows
remained active after boot. `ddb.onpanic=0` was verified; no panic was induced.
No physical playback or microphone recording was performed.

A subsequent ordinary software reboot also loaded this kernel from eMMC
through the unchanged vendor autoboot path, without serial interception or
manual `bootm`. Xorg, awesomeWM, Ethernet and MCU keepalive restarted. Audio
routes, stream open/active counts and sample counters remained disabled/zero.
This is a warm reboot check, not cold power-loss or sustained-use acceptance.

## Remaining acceptance

Physical IRQ streaming, latency/CPU use, underrun/overrun behavior,
amplifier polarity, acoustic level, microphone speech quality, duplex and
the voice application remain unverified. Suspend/resume and DMA are outside
this implementation. The portable fixture does not execute the complete
allocation/free or mixer/IRQ establishment lifecycle; those paths were
compiled and reviewed. Longer uptime is not sustained stability acceptance.
