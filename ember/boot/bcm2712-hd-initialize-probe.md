# Explicit BCM2712 shared HD control initialization

The [opt-in module](bcm2712-display-hsm.md#finite-opt-in-module) accepts
`hd-mode=initialize0`. Missing properties preserve the existing modes.
Unknown, empty, non-string or conflicting HD/clock/DVP/HPD/DDC properties fail
before effects. This mode does not call BVB or PHY APIs and does not advance
to another mode. It is a source-tested programming boundary, not physical acceptance.

Use a coherent selected kernel and module. Record the source, generated options,
headers, build settings, ABI sample and strong symbols. Buildinfo alone does not
establish ABI. The existing render, valid FDT and cold-headless framebuffer guards
remain mandatory. An enabled inherited HSM rate of at least 120 MHz is required;
the mode does not bootstrap an off, zero-rate or low-rate clock.

Before execution, establish controlled exclusion of **all shared-HD consumers**,
including firmware, audio and both HDMI ports. Disabled VID_CTL video does not
prove audio idle. Absence of a framebuffer, a prior EDID result or these sampled
registers does not establish exclusive ownership or accessible power domains.
No property can substitute for that external prerequisite.

Set `kernel_commit` from the verified kernel receipt before this explicit command:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    -s hd-mode=initialize0 \
    /absolute/path/bcm2712_hsm_probe.kmod
```

## Fixed sequence

1. Verify inherited enabled HSM, acquire a 120 MHz lease, and recheck state, rate,
   minima, lease, fault0 and mutation0.
2. Create the shared resource10 DVP owner. Require both inherited gate bits3/4
   clear and reset bits1/2 clear. Acquire port0 and verify cached lease1 before
   port1. Acquire port1 and verify leases3. Both checks require unchanged gates,
   resets, fault0 and mutation0.
3. Map canonical resources11,17,25. Read shared HD11 offsets0,0x44,0x48 and
   controller DVP17/25 offset0xbc. Require both VID_CTL.ENABLE bit31 clear and
   both CLOCK_STOP.PIXEL bit1 set. The initial DVP_CTL stays raw and unknown.
4. Set `write_attempted` before one whole-register write of zero to HD11+0.
   Complete a read/write barrier over resource11 before any readback.
5. Read the same five words again, once each. Require DVP_CTL0, both video
   enables clear and both pixel-stop bits set. No cached pre-value substitutes
   for this fresh readback.
6. Only after the verified readback, unmap25,17,11. Release DVP port1 then port0
   with cached checks, destroy DVP and unmap10. Release and destroy HSM last.

There are exactly ten controller reads, one HD11 write and one HD11 barrier on
the accepted path. Resource11 is `0x107c720000/0x100`; resources17/25 are
`0x107c701000/0x200` and `0x107c706000/0x200`.
No SW_INIT reset, VID_CTL, CLOCK_STOP, IRQ-mask or PHY write occurs.
Never clear BCM2712 VID_CTL.ENABLE. There is no restoration of the initial
DVP_CTL value. Software cleanup leaves the inherited DVP gates open and HSM enabled.

A failed pre-snapshot predicate causes zero HD writes, normal known-prefix cleanup,
and an error return. A post-write mismatch or a status, map or cleanup failure
stops without retry and retains remaining maps, native owners and leases.
Logs distinguish the attempted write and last complete cached snapshot.
The native owners keep only kernel-resident callbacks. No callback points into
this module. Failed INIT and FINI0 can remove module text while those native
owners remain. A controlled reboot releases uncertain ownership.

The expected no-clock/no-gate-write path assumes no concurrent writer.
An external change can make a native acquire write before its post-check detects
mutation. That condition stops before HD maps. The normal-active-SoC assumption
comes from pinned Linux prior art; it is not an ACPI power contract.
Native bus_space cannot recover a stalled transaction or synchronous bus abort.
The finite sequence does not impose a hardware-access deadline.

Return0 means that this fixed control write, fresh readback and software cleanup
completed. It does not establish a full reset, power-off, idle, universal handoff,
PLL lock, link, scanout or safe PHY ownership. Further BVB and PHY work needs its
own explicit acceptance and controlled lifetime.

## Source basis and checks

Raspberry Pi Linux
[`a1a601d4857978d810175365550c4536701df4ed`](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed)
uses a whole DVP_CTL0 write in
[`vc5_hdmi_reset`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L210-L235).
The BCM2712 table maps it to
[shared HD+0](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_regs.h#L438).
The [DVP reset provider](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-bcm2711-dvp.c#L32-L53) leaves `reset_us` zero;
[`reset-simple`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/reset/reset-simple.c#L66-L81)
then rejects the preceding reset request before a reset-register access.
The Linux caller ignores that return. This probe does not invent an SW_INIT pulse.
The subsequent Linux CLOCK_STOP OR is omitted because both pixel-stop bits are
already required and rechecked. This subset is not full Linux reset equivalence.
The mechanism uses interface facts without copying GPL implementation code.

```sh
HSM_PROBE_SKIP_MUTANT=1 sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The actual-source fixture uses the real module, resource decoder, clock/DVP cores,
native owners and libfdt. Kernel services and MMIO are controlled. It checks exact
accesses, held leases, the write latch before the native void write, all pre- and
post-predicates, fresh readback, reverse cleanup and retained uncertainty.
Focused causal mutants detect missing guards, wrong access, barrier, readback,
retention and error propagation. Source checks and sanitizers do not establish
physical HD initialization or subsequent BVB/PHY acceptance.
