# Native held clocks and two-pipeline stop

The manual `EMBERV3DCONSOLE` profile extends its existing CPU serial close and
firmware notification. After successful `bcmv3d_render_attach`, the same resident
boot owner runs one fixed continuation. Default profiles remain unchanged.
This is a bounded stop of both inherited firmware PV/HVS pipelines. It is not KMS, display
ownership, an AXI drain, a cache/MMU exclusion proof or a suspend interface.

## Admission and lifetime

The private native function checks its actual ACPI softc, CLOSED phase, successful
renderer attachment and attempted/complete/error-free firmware result. It then
queries the exact issued lease's successful complete CPU close/drain and the real
MI-held serial route. A raw CPU close or copied firmware value facts cannot
replace that route. All gates precede clock GETs and register mappings.

Under Giant, the owner records attempted before a provider can allocate or yield.
The finalizer's complete Giant recursion is released for clock calls, maps and
delays. Its exact depth is restored before terminal publication. Reentry reads
only attempted/terminal fields under Giant and stops before in-progress fields.
No DRM memory lock, cache lock or spinlock is held across these operations.

The boot profile has resident active devices and no suspend or detach path. It
admits no competing display driver or manual display probe. There is no invented
power token or generic caller permission flag. Firmware ACK alone does not prove
hardware exclusion. The firmware notification is global. The firmware can leave
both PVs and HVS0/1 active with one connected display. The fixed consumer covers
both pipelines and refuses an active HVS2 or unsupported PV clock source.

All created typed providers, acquired leases and completed mappings remain in
this owner until reboot, including every failure. There is no release, unmap,
rollback or retry. The first terminal error survives subsequent calls. CLOSED
console routing and render operation remain retained; genfb fallback cannot run.

## Held clocks

All four GET-only creates complete before the first acquire. The fixed builtin
registry owns HSM13, BVB14, CORE4 and DISP16 through one transport mutex.
The HSM minimum is at least 149985000 Hz and its inherited positive rate. BVB is
at least 75000000 Hz, its firmware floor and its inherited positive rate. DISP uses
its firmware floor and inherited positive rate. CORE's floor already includes
its inherited positive rate. These choices do not lower inherited rates before
the stop. Each acquire uses the existing final wire GET state/rate checks. The
consumer then requires cached state 1, adequate rate, slot 0 lease and no fault.
It adds no independent clock engine or refresh API.

## Fresh registers and bounded order

The validated ACPI contract supplies exactly resource 3 HVS, 5 PV0 and 6 PV1 through
the real attachment bus tag. The first read is HVS VERSION. Low byte 0x53 selects
C0 CTRL/STATUS 0x30/0x44 with stride 0x20. Low byte 0x54 selects D0 offsets 0x100/0x118
with stride 0x40. Any other generation stops before other register reads.
Fresh observations include global HVS control, all three channel control/status
pairs and both PV control/video pairs.

Before writes, refuse enabled or non-disabled HVS channel 2 and clock selection
other than 0 in bits 3:2 for either PV. Disabled components remain observational.
Active components use this phased order:

1. Read each active PV+4 and clear VIDEN bit 0 with RMW. Barrier and read back.
   Poll each for at most 100 delays of 10 us. Finish both before the next phase.
2. Delay 20 ms once, after the last active VIDEN has cleared.
3. For each active PV, read PV+0 and clear EN bit 0 with RMW. Barrier and verify
   disabled. Set FIFO_CLR bit 1 from the fresh readback, barrier and read back.
4. Only after both PVs are disabled, stop HVS0 and HVS1. Read each active channel's
   CTRL and set RESET bit 30. Barrier. Read again and clear ENB bit 31. Barrier,
   then poll ENB 0 and STATUS MODE bits 14:13 equal 0 for at most 1 ms per channel.

This ordering does not depend on a guessed firmware HVS-to-PV route. Both pipelines
must finish before the overall result is complete. Every failure stops all later
I/O. Each pipeline retains its last raw PV/HVS values, stage, mutation-attempted,
first error and completion fields. A second-pipeline failure can retain a completed
first channel. It cannot turn the overall result into success or permit retry.

RESET need not self-clear. EMPTY bit 22 is logged only as an observation. Global
HVS enable is never changed. The operation writes no HVS2, HDMI
VID_CTL, encoder, PHY, shared cache, MMU4, scanout table or framebuffer memory.
Bus probing errors and partial writes are terminal. Diagnostic output uses only
cached observations after a failure; it does not add fresh I/O.

## Source checks and acceptance boundary

```sh
ruby ember/tools/bcm2712-display-quiesce-contract.rb "$PWD"
ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
SKIP_MUTANTS=1 CFLAGS='-DNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer' \
    ruby ember/tools/bcm2712-display-quiesce-contract.rb "$PWD"
SKIP_MUTANTS=1 CFLAGS='-DNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer' \
    ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
```

The pure fixture compiles the actual mechanism separately. The joined fixture
extracts unchanged native owner/finalizer/accessor bodies and composes the actual
MI/simplefb/PLCOM close, firmware protocol, builtin clock adapter and shared core.
Only kernel scheduling, ACPI, mailbox and bus services are controlled. Tests cover
both finalizer orders and generations, exact admission, raw/foreign/stale leases,
mailbox reentry, all GET/map/read/write failures, first-error retention, off-clock
activation, inherited rates, PV1-only and both-active states, phased ordering,
HVS2/clock-source refusal, second-pipeline faults, timeouts and persistent RESET. Compiled causal controls must reach a named fatal CHECK. `NDEBUG` does not
disable CHECK. These host tests do not prove real bus timing or hardware exclusion.

`QUIESCE_EVIDENCE` and `SERIAL_HANDOFF_EVIDENCE` select new evidence directories.
`SKIP_MUTANTS=1` runs positives. `CC`, `CFLAGS` and a one-executable `TEST_RUNNER`
select compilation/execution. Native full-TU analysis, coherent kernel/module
linkage and physical acceptance of this continuation remain pending. Prior CPU
serial close/firmware ACK and unified clock registry kernel/module linkage pass.

Register and ordering facts come from pinned Raspberry Pi Linux
[a1a601d](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4):
[vc4_regs.h](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h),
[PV disable/reset](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L505-L549)
and [HVS6 stop](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1095-L1122).
The [global notification and all-CRTC disable](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_drv.c#L395-L425)
and [both BCM2712 PV definitions](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L1330-L1356)
cover the two firmware pipelines. Only facts inform this independent BSD implementation. No GPL implementation code
is copied.
