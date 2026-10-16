# CIX P1 H.265 hardware encoder

Status: proposal. Implementation, firmware execution and hardware acceptance are pending.
Origin: EmberBSD contributors, AI-assisted.

## Problem and scope

EmberBSD has no CIX P1 video codec driver. Its video capture interface does
not implement the two queues required for memory-to-memory encoding.
The target is Orange Pi 6 Plus with CIX CD8180 and stock ACPI firmware.
The OS owns firmware validation, DMA mappings, interrupts, power/reset,
job lifetime and the documented application interface. Ports owns FFmpeg
and GStreamer consumers. No Linux binary driver is an EmberBSD implementation.

Initial OS baseline: `7fac11898f3451472b6288d84ea3fdc87ca4f808`.
The selected reference is the maintained Sky1 Linux 6.18 LTS patch track at
`57e018a398248d7e5e4d798610df79a557c0629f`. The retired DKMS repository
is historical evidence only. Preserve Arm China/CIX authorship and licences.
Do not relabel GPL source as BSD. An original native implementation must
identify the functional wire format it implements and its source references.

The firmware source is Sky1-Linux/sky1-firmware at
`dd81690747ddb092bcc2a221daa0f8f679ee4bc4`, `firmware/hevcenc.fwb`.
The upstream README describes proprietary redistributable firmware; this
proposal does not import or relicense the binary. Record the selected blob's
hash and the actual applicable licence before any package redistribution.

## Required behavior

The encoder accepts frames from memory and emits H.265 access units.
Keep firmware addresses separate from host addresses and DMA bus addresses.
Use bus_dma synchronization and bounded waits. Do not assume IOMMU bypass,
UEFI clock state or a reference board's power configuration.
An absent, truncated or incompatible firmware must fail before hardware use.
Reset and close during a frame must release resources only after DMA stops.
Late completion must not reach a closed or reused session.
FFmpeg and GStreamer must explicitly select the hardware backend; a CPU
encoder result does not establish this driver.

## Acceptance

Cross-build with the common GCC16 compiler. Test parser boundaries, queue
wraparound and invalid firmware responses with the actual shared core source.
Run Cppcheck, GCC path analysis, Clang analysis and supported memory checks.
These checks do not establish device DMA, power, firmware or encoding.

On Orange Pi 6 Plus, record board revision, firmware, source, selected
kernel/modules and all artifact hashes. Verify firmware boot and the selected
hardware encoder. Encode two concurrent 2048x1500, 8-bit streams at 9 fps,
at most 4 Mbit/s per stream, for 30 minutes under parallel CPU load.
Record frame counts, timestamps, per-frame encoding latency and decoder output.
Exercise missing firmware, block reset and close in the middle of a frame.
H.264 and decoding require separate acceptance records.

## Dependencies and recovery

The board needs a repeatable boot, working memory and a transport for results.
Resolve ACPI resources, clock/reset ownership and DMA translation before
submitting work. The current SCMI implementation does not supply a CIX clock
or reset provider, and the capture driver is not an M2M framework.
Keep these gaps explicit instead of reporting an attached device as an encoder.

Use a separately selected test kernel and preserve the accepted boot choice.
Do not overwrite the running base, firmware or persistent application data.
Retain the original boot artifacts and a working UART recovery path.
Device writes and restarts require the relevant test scope and actual access.

## References

- [Maintained Sky1 kernel patches](https://github.com/Sky1-Linux/linux-sky1/tree/57e018a398248d7e5e4d798610df79a557c0629f/patches).
- [VPU reference](https://github.com/Sky1-Linux/linux-sky1/blob/57e018a398248d7e5e4d798610df79a557c0629f/patches/0010-media-cix-Add-Sky1-video-codec-VPU-driver.patch).
- [Firmware provenance](https://github.com/Sky1-Linux/sky1-firmware/tree/dd81690747ddb092bcc2a221daa0f8f679ee4bc4).
- [Ports common media](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media).
