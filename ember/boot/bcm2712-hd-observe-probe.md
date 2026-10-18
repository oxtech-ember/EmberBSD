# Finite BCM2712 HD observation

The [opt-in module](bcm2712-display-hsm.md#finite-opt-in-module) accepts
`hd-mode=observe`. Missing properties keep the existing modes. Empty, unknown,
non-string or conflicting HD/clock/DVP/HPD/DDC modes fail before any effect.
Use the selected-kernel, render, valid FDT and cold-headless framebuffer guards.
Audit the module against the exact kernel source, headers, options and strong symbols.
Buildinfo alone does not establish ABI.

The initial HSM state must be enabled, with rate at least 120 MHz, no leases,
no fault and no mutation. A zero or disabled clock fails before acquisition or SET.
The observer does not bootstrap HSM. A separately accepted HSM-only cycle can
establish that prerequisite. It does not authorize HD access.

Set `kernel_commit` from the verified kernel receipt before this command:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    -s hd-mode=observe \
    /absolute/path/bcm2712_hsm_probe.kmod
```

The sequence is finite:

1. Acquire HSM port0 with a 120 MHz minimum. Recheck state, rate, lease, minima and mutation0.
2. Create the shared DVP owner. Require inherited gate bits3/4 clear and reset bits1/2 clear.
3. Acquire DVP port0. Verify cached lease1, fault0, mutation0 and unchanged gates/resets before port1.
4. Acquire DVP port1. Verify cached leases3, fault0, mutation0 and unchanged gates/resets before mapping HD.
5. Map canonical resources11,17,25. Read exactly the five words below, once each.
6. Unmap25,17,11. Release DVP port1 then port0, with status checks. Destroy DVP and unmap10.
7. Release HSM, verify status and destroy it. Do not query a freed owner.

| Resource | CPU address / length | Read offsets |
| --- | --- | --- |
| 11, shared HD | `0x107c720000/0x100` | `0`, `0x44`, `0x48` |
| 17, HDMI0 DVP | `0x107c701000/0x200` | `0xbc` |
| 25, HDMI1 DVP | `0x107c706000/0x200` | `0xbc` |

The raw fields are DVP_CTL, VID_CTL0, VID_CTL1 and the two CLOCK_STOP words.
The reset-side candidate means DVP_CTL0, both VID_CTL.ENABLE bit31 clear,
and both CLOCK_STOP.PIXEL bit1 set. Reserved bits remain raw data.
A noncandidate snapshot still completes normal cleanup. Return0 means that sampling
and cleanup completed. It does not mean idle, exclusive handoff, safe PHY access or output.
HDMI_ACTIVE0 cannot prove idle because an active DVI output can have that value.
No BVB call, controller write, reset pulse, PHY, IRQ or automatic next mode follows.

The normal-active-SoC assumption follows the pinned Linux runtime-resume path.
It is not an ACPI power-domain or general accessibility contract.
Cold-headless state and absence of a framebuffer do not exclude a firmware writer.
The accepted no-write path assumes no concurrent writer. An external clock or gate
change can cause native acquire to write before the post-check detects mutation.
That condition stops before HD maps and retains the remaining owners.
Native bus_space cannot recover a stalled transaction or synchronous external abort.
The log identifies first HD/controller MMIO before the first read.

A status, map or cleanup failure stops without retry or blind restoration.
Logs identify residual mappings and the validity/stage of cached samples.
An incomplete map sequence does not publish a valid five-word sample.
Kernel-native callbacks retain no module or stack pointer. Failed INIT and FINI0
can remove module text while native owners remain. A controlled reboot releases uncertain ownership.

The facts use Raspberry Pi Linux revision
[`a1a601d4857978d810175365550c4536701df4ed`](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed):
[`vc4_hdmi.c`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L3096),
[`vc4_hdmi_regs.h`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_regs.h#L437), and
[`vc4_regs.h`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L1029).
The native probe uses interface facts and does not copy the GPL implementation.

Run the actual-source fixture with:

```sh
HSM_PROBE_SKIP_MUTANT=1 sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The fixture uses the real module, resource decoder, clock/DVP cores, native owners
and libfdt, with controlled kernel services and MMIO. It checks exact ranges,
five reads, both held leases, reverse cleanup, all map failures, retained uncertainty,
external-writer mutation, mode refusals and candidate/noncandidate samples.
These source checks do not establish a linked module or physical HD/PHY acceptance.
