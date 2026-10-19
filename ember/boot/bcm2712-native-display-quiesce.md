# Native held clocks and PV0/HVS0 stop

The manual `EMBERV3DCONSOLE` profile extends its existing CPU serial close and
firmware notification. After successful `bcmv3d_render_attach`, the same resident
boot owner runs one fixed continuation. Default profiles remain unchanged.
This is a bounded stop of the inherited port0 pipeline. It is not KMS, display
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
hardware exclusion. The fixed consumer refuses conflicting fresh pipeline state.

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

Before writes, refuse active PV1, enabled or non-disabled HVS channels 1/2 and
PV0 clock selection other than 0 in bits 3:2. A consistently disabled PV0/HVS0
succeeds without writes or delay. Otherwise:

1. Read PV0+4 and clear VIDEN bit 0 with RMW. Barrier and read back. Poll for at
   most 100 delays of 10 us. A timeout ends all later MMIO.
2. Delay 20 ms. Read PV0+0 and clear EN bit 0 with RMW. Barrier and verify disabled.
   Set FIFO_CLR bit 1 from the fresh readback, barrier and read back.
3. Read HVS0 CTRL and set RESET bit 30. Barrier. Read again and clear ENB bit 31.
   Barrier, then poll ENB 0 and STATUS MODE bits 14:13 equal 0 for at most 1 ms.

RESET need not self-clear. EMPTY bit 22 is logged only as an observation. Global
HVS enable is never changed. The operation writes no other pipeline, HDMI
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
activation, inherited rates, second-pipeline refusal, timeouts and persistent
RESET. Compiled causal controls must reach a named fatal CHECK. `NDEBUG` does not
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
Only facts inform this independent BSD implementation. No GPL implementation code
is copied.
