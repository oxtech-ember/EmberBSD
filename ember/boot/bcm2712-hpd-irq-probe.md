# Finite HPD0 IRQ probe

The explicit `hpd-mode=irq0` mode uses the existing finite HSM probe and native
AON IRQ provider. It is not a display driver. Use the selected kernel's real
headers and strong IRQ symbols. Extend the existing ABI sample with mutex,
condition-variable and timespec layouts before physical acceptance.

Use the same controlled cold-headless boot, exact buildinfo, framebuffer and
render-device guards as the [HSM probe](bcm2712-display-hsm.md). The operator
must keep HDMI0 connected for the initial HPD sample. Conflicting, empty,
unknown or non-string mode properties fail before HSM, mapping or MMIO.

The probe holds HSM at least 120 MHz and a DVP0 lease. It requires inherited
DVP gate bit3 clear, reset bit1 clear, no fault and no DVP mutation. It maps
HDMI0 resource16 and AON resource12. Initial reads require raw HPD bit0 at
`+0x1c8` set and owned AON bits7/8 masked at `+0x0c`. It logs initial
pending bits at `+0x00`. Owned masked sticky bits are accepted. The native
provider masks and W1C-acknowledges them through its normal IRQ service.
A stale callback that samples raw HPD 1 does not advance the level cycle.
The native provider owns only `0x180` on GSI271. Its MASK_SET,
MASK_CLEAR and W1C writes stay within that mask. The accepted path sends no controller,
DDC, DVP, PHY or reset write. If an external owner changes the gate, native
DVP acquire can attempt a gate write. Its mutation flag aborts before either
IRQ/controller mapping and retains the native owners and shared mapping. It does not use AON SW_SET or PCI-side registers.

The load waits for IRQ-driven raw HPD samples `1->0->1`. After the armed log,
disconnect HDMI0 for at least five seconds, then reconnect it while the load
is pending. Each process-context callback reads raw HPD once and records
pending bits, raw value, service error and phase. Leaf labels, duplicate
samples and a coalesced edge cannot replace the observed level cycle.

A fixed 120-second monotonic wait budget includes IRQ creation. Spurious CV
wakes do not restart it. There is no HPD polling. The deadline bounds the wait
for the sampled cycle; it cannot interrupt stalled MMIO, a callback, scheduler
latency or lifecycle teardown. Timeout, interruption and service errors use
normal teardown and return an error without a success marker.

Teardown destroys and drains the IRQ provider without holding the callback
mutex. It then unmaps AON and HDMI0, cleans up DVP, and cleans up HSM. A success
marker means the required sampled cycle and lifecycle completed. A pre-destroy
IRQ error sample is not a post-drain proof of complete hardware health.

If an unexpected destroy error prevents drain, the module keeps its static
callback state, mutex, CV, native owners, maps and render reference. INIT
returns zero to keep the module resident, but logs a retained fault and no
success. FINI returns `EBUSY`. This state requires a controlled reboot.

The actual-source probe contract tests both DDC feature configurations and
all earlier modes. It controls the IRQ lifecycle API and kernel time/CV
services. Separate actual-source contracts test the native IRQ provider.
Software checks do not establish a physical HPD cycle, ACPI power, KMS output
or a Plasma session. Native bus_space stalls and aborts remain unrecoverable.

## Physical boundary

One finite cycle passed on CM5 Rev 1.0 with BCM2712D0 on 2026-10-18.
Kernel `6e548fea09ffc04cb844c0d55c799ce25173a74b` and firmware `c90c2a6c`
were loaded before this test. The module was built from `0df8d45c` with
GCC 16.2.0. Source headers, generated options, 50 ABI values and strong
symbols matched the selected kernel. The 57416-byte module has SHA256
`8e05b1b8ab55e25946efca4fdac21dba16aa6f0005761072ed99e3d34b326fcb`.

The controlled boot had no firmware framebuffer. HDMI0 was connected after
boot. Initial AON status was `0x80`, mask was `0xffff`, and raw HPD was 1.
GSI271 delivered three callbacks: stale `0x80/raw1/phase0`, disconnect
`0x100/raw0/phase1`, and reconnect `0x80/raw1/phase2`. Every service error was
zero. The sampled disconnected interval was 5.710058 seconds. The cycle
completed 79.997706 seconds after arming, within the fixed wait budget.

IRQ drain, controller unmap and DVP/HSM cleanup completed. Module unload
succeeded. Temperature was 50.469 C before the cycle, 51.569 C after it, and
50.469 C after unload. No throttling or undervoltage was reported.
Repeated cycles, DRM connector notifications, display power transitions,
PHY, scanout, KMS and a physical Plasma session remain unverified.
