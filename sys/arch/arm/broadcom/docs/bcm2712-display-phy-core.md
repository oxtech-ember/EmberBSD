# Fixed VC6 HDMI0 PHY mechanism

The [pure core](../bcm2712_display_phy.c) issues one
fixed HDMI0 RGB8 profile with a 148.5 MHz TMDS character rate.
The intended first mode is progressive 1920x1080 without pixel repetition.
The core does not validate geometry, a sink, EDID, mappings, clock leases,
power or controller quiesce. The explicit [retained programming module](bcm2712-phy-program-probe.md)
links this core privately. It adds no built-in provider or automatic attachment.

## Private interface

[`bcm2712_display_phyvar.h`](../bcm2712_display_phyvar.h)
accepts only typed PHY and RM callback windows. It exposes no register list,
arbitrary frequency or profile API. The future native owner must validate
canonical resource18 (`0x107c701d00/0x300`) and resource19
(`0x107c702000/0x80`) before access. The caller serializes calls and keeps
callback storage and its argument alive until it no longer uses the object.

Start with fresh zero-initialized storage. Init validates callbacks and
performs no I/O. It rejects repeated init, including after programming,
disable or fault. The IO argument can alias the object's IO member.
Cached status performs no I/O. Init and status preserve output on error.
Never pass uninitialized storage to these APIs.

`bcm2712_phy_program_148500` is one-shot. It issues 25 fixed writes, then two
fresh read/modify/write pairs on PHY+0x190. Only PLL_RESETB bit0 changes in
those pairs. The sequence uses 27 writes and two reads. Each successful
write has a mandatory ordering barrier for both windows. No delay, polling,
PLL-lock predicate, SW_INIT, VID_CTL or other controller operation exists.
The finite callback count cannot bound a stalled callback or native MMIO.

`bcm2712_phy_disable` is allowed only after full programming. It issues three
writes: PHY reset0, analog power0, and post-divider bypass. It clears
`programming_complete` when disable starts. `disable_complete` means only
that all three writes and barriers completed. It does not restore RM/PLL/lane
state, prove full power-off, or authorize mapping/clock/owner release.
A second program or disable cycle is rejected. There is no fini/free API.

## Errors and lifetime boundary

Read and write callbacks return positive BSD errno. The mandatory barrier
has no error return; it cannot recover a stall or bus abort.
`mutation_attempted` is set before each write callback. An operation error
is sticky. Later program/disable calls return the fault without any further
callback, including a barrier or cleanup write. Cached status remains usable.
It preserves the last operation stage and attempted step. Read/write counters
count completed callbacks across program and disable.
`programming_complete` means PROGRAMMED, not PLL lock or physical link success.

A future native lifecycle owner needs live HSM>=149985000 Hz,
BVB>=75000000 Hz and DVP0 leases, accessible powered windows, and an exclusive
shared-HD/controller handoff that excludes firmware and HDMI1 conflicts.
Neither these cached leases nor absence of framebuffer proves quiesce or power.
It must validate the current sink and selected mode; historical EDID is not
an unconditional authorization for a later monitor.
The safe post-disable owner-release predicate remains open.
The retained module is a controlled access test under the documented prior-art
assumption. It does not complete those external power or ownership contracts.
Never clear BCM2712 VID_CTL.ENABLE or infer a supported SW_INIT reset pulse.

## Source checks and provenance

```sh
sh ember/tools/bcm2712-display-phy-contract.sh "$PWD"
PHY_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-phy-contract.sh "$PWD"
```

The fixture compiles the actual core and uses a fixed register-fact oracle.
It checks both window bounds, every value and operation, fresh unrelated RMW
bits, all29 program and3 disable failure positions, both errno values,
reinit/one-shot rules and no callbacks after fault. Causal mutants alter these
contracts. `CC`, `CFLAGS`, `PHY_COMPILE_ONLY`, `PHY_OUTPUT` and `TEST_RUNNER`
support cross builds and supported memory checkers. An actual `_KERNEL` TU
compile is separate. For this new file, paired analysis uses a header-only TU
to identify inherited header findings; it is not a previous PHY implementation.
Software checks do not establish physical clock, PHY, link, scanout or KMS success.

The implementation uses register facts from Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed` without copying GPL implementation code:
[VC6 sequence and disable](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_phy.c#L945-L1206),
[offset map](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_regs.h#L484-L508),
[clock minima](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L1462-L1505), and
[BCM2712 VID_CTL restriction](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L851-L860).
