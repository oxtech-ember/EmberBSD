# BCM2712 V3D reset takeover probe

This opt-in probe advances physical V3D identification to exclusive native
reset ownership. It does not allocate DMA memory, program the MMU, submit
TFU/CSD/CL work, expose DRM, or establish graphics acceleration.
The normal `EMBER64` configuration remains a passive, unclaimed observer.
`EMBERV3D` adds `BCM2712_V3D_TAKEOVER` explicitly.

## Design and boundary

The existing ACPI observer validates GPU resources, firmware clock state/rate,
PM readiness and V3D 7.1 identification first. The takeover then claims the
existing BCM2712 PM mapping; it never creates another PM mapping. The claim
requires the exact PM physical base and size used by the recorded CM5 ACPI.
Only one device can claim the reset interface. A second claimant is rejected.

Persistent HUB, CORE and SMS mappings belong to the claimant. Before writing,
the probe records identity, MMU, TFU, CSD, GMP and interrupt state. It requires
the observed initial SMS combination REE=0/TEE=0x50, no active TFU/CSD work,
and a disabled, non-flushing MMU. Other SMS modes are unsupported by this
first destructive probe, even if the passive observer can identify them.
These observations are eligibility checks, not proof that firmware cannot
submit future work. The explicit reset operation establishes the native
takeover; the probe does not claim a firmware ownership protocol exists.
CT0CS, CT1CS and PCS are inventory only: the pinned V3D 7.1 sources do not
define their active-state fields. There is no proven no-active-CL gate.
This opt-in initialization reset may cancel unknown firmware command-list
work and does not resume it. It is for controlled board bring-up without a
preceding user GPU session and with a verified boot recovery path.

Before the first write, the PM claim is sealed until reboot. The probe masks
only defined V3D 7.1 HUB/CORE interrupt fields through their W1S registers,
checks the mask readback and preserves other fields. It does not restore
previous masks through W1C registers. There is no interrupt handler.

SMS clear-power-off and REE reset commands have bounded polling and validated
mode/status fields. Documented SEQ_PC/HUBCORE_STATUS fields may show progress;
completion still requires exact REE=0/TEE=0x50. The existing PM owner then
asserts only V3DRSTN bit 6,
checks the complete value, waits at least one microsecond, and deasserts it.
PM writes include the hardware password and preserve all other readable
bits. Clock state must already be enabled with a nonzero configured rate;
this probe changes neither frequency nor clock state.

After reset the probe masks and checks interrupts again, records another
inventory, and verifies identity, SMS, PM and the defined MMU/TFU/CSD/GMP
eligibility fields. This does not prove CL inactivity, DMA leases, or future
firmware exclusion. A
failure after the first attempted write retains all mappings and ownership;
there is no retry, detach, power-off, or speculative rollback. Success also
retains ownership and mappings until reboot. No DMA lifetime is implied.
Fault-aware MMIO handles recoverable synchronous faults; asynchronous SError
and a stalled interconnect remain outside its guarantee.

The ACPI platform sets `faa_bst` to `arm_generic_bs_tag`; `acpi_fdt` passes
it through `aa_memt` to both drivers. In `aarch64/aarch64/bus_space.c`, that
tag's `bs_po_4` is `generic_bs_po_4`, which uses `cpu_set_onfault` around
`generic_dsb_bs_w_4`. The assembly writer executes `dsb sy` after its store
and before returning to `cpu_unset_onfault`. The explicit bus-space barrier
and readback follow each successful write. This is not the unused
`bs_notimpl_bs_po_4` entry.

## Source facts

The register and lifecycle reference is Raspberry Pi Linux
[`43c132e8863c3bff3647033b6a7d2bf87b15501c`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c):
`drivers/gpu/drm/v3d/{v3d_drv.c,v3d_gem.c,v3d_regs.h,v3d_drv.h,v3d_debugfs.c}` and
`drivers/pmdomain/bcm/bcm2835-power.c`. The implementation here uses native
EmberBSD bus-space, locking and error propagation; it does not import Linux
driver code.

For BCM2712 the Linux PM node has no ASB mapping or domain clock. Its reset
path therefore changes PM_GRAFX_2712 bit 6, with the one-microsecond delay
before reset release. The firmware-clock ops have no enable/disable callback;
their existing prepared state is read from firmware. This does not justify
inventing a SET_CLOCKSTATE or power-domain mailbox transaction.

The first physical sample was V3D 7.1 on CM5 Rev 1.0, BCM2712 D0, 4 GiB.
HUB_IDENT1=0x81117 is also present in Mesa 26.2.4's `v3d_noop.c` V3D 7.1
profile. Its old WITH_TFU bit is not used as an absence gate. TFU remains
unexecuted. CORE_IDENT1 is read independently; HUB_IDENT1 is not substituted
for it. The complete identification receipt remains in
[the passive observer guide](bcm2712-v3d.md).

The R5 physical inventory reported GMP_STATUS=0x30 and ERR_STAT=0x1000.
Whole-register zero checks incorrectly rejected these values before any
write. GMP bits 4/5 are RD_ACTIVE/WR_ACTIVE, distinct from outstanding read
and write counts. The Linux `v3d_idle_axi` condition checks count fields
and CFG_BUSY; it does not check these activity bits. That drain function is
disabled in the reset path and is not evidence of a verified V7.1 drain.

For ERR_STAT, Broadcom's
[VideoCore IV Architecture Guide](https://docs.broadcom.com/doc/12358545),
table 87 on page 100, defines bit 12 VCDI as a read-only VCD idle indication.
The pinned Linux V3D driver retains the same named bit without a V7-specific
alternative. Applying that meaning to V7.1 is an inference from the current
driver and the earlier specification, not a claim to have a public V7.1
field specification. Mesa's V7.1 simulator register header is supplied by
the external simulator and is not present in the public Mesa source tree.

The narrow eligibility policy allows only GMP bits 4/5 and ERR_STAT bit 12
to be nonzero. All outstanding-count, CFG_BUSY, protection/error/reset and
unknown bits retain their rejection. The raw values remain in the inventory.
Neither permitted status proves AXI drain, CL inactivity, or firmware
exclusion. No other eligibility or write condition changes.

## Software checks

Run both actual-source contracts on the development host:

```sh
CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/bcm2712-v3d-contract.sh
CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/bcm2712-v3d-takeover-contract.sh
```

Both also run from `kernel-contracts.sh` in portable and all modes. The
passive contract compiles without the takeover option. The takeover contract
compiles the actual production PM/reset and takeover function bodies with
fake bus services and a pthread-backed native-claim mutex. Its independent
write allowlist rejects MMU/submission, W1C, power-off and unrelated PM writes.
This simulates recoverable failures; it cannot test physical interconnect
fault delivery or firmware exclusion.

The macOS ASan/UBSan run passed 186 passive cases with nine rejected causal
mutants, and 463 takeover cases with fourteen rejected causal mutants. Tests
cover every read on the success path, every mapping/write failure, failed
writes both before and after simulated hardware effects, all SMS mode/flag
bits, documented transient progress, bounded timeout, post-reset corruption,
concurrent claims, reset delay/order, quarantine and forbidden writes.
The complete R5 register snapshot first failed against the unchanged
production source, then passed with the two status masks. Both old
whole-register zero checks are independently rejected causal mutants.
All other GMP and ERR_STAT bits are injected before and after reset and
must still stop the probe. The SSH dmesg receipt SHA256 is
`56450c7765791190c4428385c8efdeecf490652003f53d97fac3551d26f83de2`.

For a target-ABI contract binary, set `CC`, `V3D_TAKEOVER_OUTPUT` and
`V3D_TAKEOVER_COMPILE_ONLY=1`. `TEST_RUNNER` names a wrapper that executes
one supplied binary path. `V3D_TAKEOVER_SKIP_MUTANTS=1` runs only the positive
contract suite. Target execution validates software ABI behavior, not GPU
hardware. The test uses pthreads for the concurrent-claim case.

The GCC 16.2 static AArch64 contract passed all 463 cases on an Orange Pi
Zero 3W running EmberBSD `f85ffd420f6`. Its SHA256 was
`097bfc0cc1edb9e7b69a5c07a2cc80f15f2e74649f9adbb18b6cf342df2374d5`.
This execution used simulated V3D registers on the board's CPU; it did not
touch the A733 GPU or establish physical CM5 reset support.

## Physical CM5 acceptance

On 2026-10-09, the clean GCC16 cross build from
`8f73f416cfa96120eb02862b1adfacf97d2728ac` booted as `EMBERV3D #1`
on CM5 Rev 1.0, BCM2712 D0, 4 GiB, on Waveshare CM5-NANO-B.
The carrier PCB revision and installed UEFI source revision remain unknown.
The existing firmware was retained. The installed kernel SHA256 was
`0bfbde78e9a83b7b088110a179c6da5d1f7eefccf6588efb7d3008443abf51b3`;
buildinfo and all four matched module hashes were verified.

The probe completed the initial interrupt masks, SMS clear-power-off and
reset, the PM reset cycle, post-reset masks and the second inventory.
It reported completion at 2.309 seconds after kernel start. All 22 inventory
registers matched their pre-reset values, including V3D 7.1 identity,
MMU_CTL=0, MMUC_CTL=0x80000000, PT_BASE=0, BYPASS=0..0xfff,
TFU_CS=0x2000, TFU_SU=0, CSD_STATUS=0, GMP_STATUS=0x30,
ERR_STAT=0x1000 and both interrupt status registers=0.
PM remained 0x1040; SMS returned to REE=0/TEE=0x50.
SSH over Wi-Fi worked after boot. The successful boot receipt SHA256 is
`768334cfa301aac4ce8b3732e553a80fee08d5bac88065fe401e9d336df5da92`.

The previous ordinary kernel, modules and boot configuration were backed up
and verified on both board and host before installation. After this one
short acceptance run, the ordinary `EMBER64` bundle was restored.
This proves the reset sequence on this firmware handoff, not repeated reset,
firmware exclusion, AXI drain, DMA/MMU operation, interrupts, command
submission or graphics acceleration. The first SSH attempt timed out during
startup; the later connection retrieved the complete successful boot log.

## Implementation checklist

- [x] Keep option-free observation free of claims and all hardware writes.
- [x] Add exact-resource PM ownership, seal and single reset APIs.
- [x] Add persistent mappings, bounded SMS reset and validated PM readback.
- [x] Preserve unrelated PM and interrupt-mask fields; never restore W1C.
- [x] Exercise actual production sources with fake bus/ACPI/mailbox services.
- [x] Check no opt-in writes, second/foreign owner, every map/read/write
  failure, SMS modes/states/timeouts, PM corruption, reset ordering/delay,
  quarantine and absence of DMA/submission.
- [x] Run the passive regression and new contracts with ASan/UBSan on macOS.
- [x] Independently review, then cross-build the complete clean-commit kernel.
- [x] Record one physical reset acceptance separately from software contracts.

The next DMA experiment needs a separate design and acceptance. In particular,
_CCA=0 requires actual bus_dma mappings and CPU/GPU cache synchronization;
kernel-owned 16 KiB TFU copies need 64 bytes of source read-ahead padding
before an unmapped guard. Neither allocation nor this reset probe proves DMA.
