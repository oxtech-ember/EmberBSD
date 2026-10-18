# Retained fixed BCM2712 HDMI0 PHY programming probe

The [opt-in module](bcm2712-display-hsm.md#finite-opt-in-module) accepts only
`phy-mode=program0`. Unknown, empty, non-string or conflicting PHY/HD/clock/DVP/
HPD/DDC properties fail before effects. Missing PHY mode preserves every existing
mode. There is no automatic transition from HD initialization or a BVB clock cycle.
This is a source-tested programming boundary. It does not establish a physical link.

The module compiles the canonical [private PHY core](bcm2712-display-phy-core.md)
as a second translation unit. A narrow source path selects that C/header pair.
The selected kernel source and preprocessing contract remain unchanged.
Audit the module C, private PHY C/header and Makefile as explicit source exceptions;
audit all other dependencies, generated options, ABI values and strong symbols
against the selected kernel. No new built-in kernel API or default attachment exists.

## External prerequisites

Use the exact selected kernel, render-device reference, valid FDT and cold-headless
framebuffer guards. Buildinfo alone does not prove ABI. Establish a fresh same-boot
EDID and validate the current sink for progressive 1920x1080 at 148.5 MHz, RGB8,
without pixel repetition. This module does not read EDID or choose another profile.
A previous monitor or historical EDID does not authorize this attempt.

Establish controlled exclusion of all shared-HD consumers, including firmware,
audio, native writers and HDMI1. No framebuffer and disabled video are not ownership
or audio-idle proofs. The module freshly samples both controllers under both DVP
leases before any BVB minimization. A reset-side candidate remains a non-atomic
snapshot. These predicates do not establish ACPI power-domain accessibility.
The normal-active-SoC prior-art assumption remains explicit.

This mode is not an ordinary unloadable finite cycle. Before execution, provide a
controlled reboot path. Set `kernel_commit` from the verified kernel receipt:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    -s phy-mode=program0 \
    /absolute/path/bcm2712_hsm_probe.kmod
```

## Fixed access and lifetime

1. Require inherited enabled HSM at least 120 MHz, without a lease, fault or mutation.
   Acquire port0 with minimum149985000 Hz. Verify state, rate, minima, lease and fault.
   This expected increase can mutate HSM through its native owner.
2. Create the shared DVP owner on resource10. Require both inherited gate bits3/4
   clear and reset bits1/2 clear. Acquire port0 and check cached lease1 before
   port1. Acquire port1 and check leases3. Both checks require unchanged gates,
   resets, fault0 and mutation0. An unexpected mutation stops before HD maps.
3. Map resources11,17,25. Read the exact five HD/controller words from the
   [HD observer](bcm2712-hd-observe-probe.md). Require fresh DVP_CTL0, both video
   enables clear and both pixel-stop bits set. Unmap25,17,11 while both leases stay held.
   This mode never performs HD control initialization.
4. Create BVB with GET-only native validation. Acquire port0 with minimum75000000 Hz.
   Verify enabled state, lease1, exact requested minimum, fault0 and rate at least
   `max(firmware floor, 75000000)`. The firmware minimum can be higher, including
   324 MHz. The request is a lower bound; it does not request or guarantee a 75 MHz rate.
5. Map only canonical PHY18 `0x107c701d00/0x300` and RM19 `0x107c702000/0x80`.
   Initialize the zeroed private core with module-resident callbacks and static argument.
6. Program the fixed 148.5 MHz RGB8 profile once: 27 writes and two fresh reads.
   Each completed write orders both windows with read/write barriers.

Typed callbacks reject an unknown window, unaligned offset or out-of-range word.
They expose no property-selected register or arbitrary frequency API. Native
bus_space read/write operations have no recoverable errno result. A successful
callback means that the access and required ordering completed. A stalled access
or synchronous bus abort cannot be recovered or bounded by this callback count.
The log identifies first PHY/RM MMIO before the first access.

Before the first attempted write, the callback latches retention. This also applies
to a software bounds rejection. It retains static core/callback storage, PHY/RM
mappings, HSM/BVB/DVP owners and leases, and the render reference. A core fault
stops all further callbacks. There is no retry, reinitialization, disable-after-fault,
blind restore, shared-controller write, recovery reset, IRQ or scanout operation.
The module never calls the pure core's three-write disable sequence.
That sequence has no established owner-release or power-off predicate.

Even full programming retains every resource. `programming_complete=1` establishes
only the fixed register sequence, not PLL lock, link, safe release or lifecycle
acceptance. INIT0 registers the retained module context; **it is not success**.
The log states `overall lifecycle accepted 0`. FINI returns EBUSY. Never treat
`modload` status0 as a hardware acceptance result or attempt to force removal.
A controlled reboot is required before another attempt.

Before any PHY write, a fully sampled noncandidate with known healthy owners can
reverse-clean the prefix and return an error. A GET-only BVB create failure or a
cached clean pre-SET acquire failure can also clean normally without success.
Any unknown status, unexpected DVP mutation, ambiguous map/API failure or cleanup
failure retains the remaining context. Cached validity/stages do not claim current
hardware state. No callback points at stack storage, and an uncertain INIT does not
unload the module text that owns those callbacks.

## Source checks and limits

```sh
HSM_PROBE_SKIP_MUTANT=1 sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The fixture compiles the actual module, private PHY core, resource decoder,
HSM/DVP cores/providers and libfdt. It controls the BVB API boundary and kernel/MMIO
services. The native clock mechanism has separate actual-source contracts.
It checks exact resources, offsets, counts, both-window barriers, held leases,
firmware-floor/rounding predicates, static callback identity, every program callback
failure and retained INIT/FINI behavior. Callback failures inject a typed software
bounds rejection; they do not simulate a recoverable native bus abort.
Existing observe/initialize/clock/HPD/DDC/IRQ positives remain part of the same run.
Focused causal mutants and sanitizer/path analysis complement these checks.
They do not establish a linked kernel/module, physical PHY access, PLL lock, link,
scanout, KMS or a complete power/display lifecycle.
