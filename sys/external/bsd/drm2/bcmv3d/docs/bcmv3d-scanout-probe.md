# Explicit native HVS scanout cycle

`bcmv3d_scanout_probe` qualifies a finite mutable-fetch sequence through the
existing [native scanout owner](bcmv3d-scanout.md). It uses three private native
GEM BOs. It does not acquire display ownership, register callbacks, program
clocks, access MMIO directly or use the memory-only HVS preparation helper.

The module does not provide DRM/KMS objects. A passed cycle is not acceptance
of KMS, page-flip events, an accelerated desktop or Plasma. Physical display
acceptance requires a separately observed run on the selected board and kernel.
The host contract supplies controlled native services; it cannot establish
physical fetch or DMA behavior.

## Prerequisites and explicit admission

Boot the explicit serial-handoff kernel profile with its retained native HVS,
MMU4 and PV0 owners. Keep the original clock, memory and firmware exclusion
conditions throughout the boot. Their absence cannot be repaired by this module.
The default render-only profile is refused.

Admission requires the registered native `bcmv3d0`, render minor 128, driver
`v3d`, the matching `\_SB.GPU0` node and the canonical ACPI display resource
tuple. A driver with `DRIVER_MODESET` is refused. The existing MMU4 owner must
report complete, sealed backing, retained complete sequencing and no fault.
The existing PV0 route must report prepared, armed, routed, no fault and head 32.
The existing scanout owner must be pristine: no current or pending GEM slot,
zero prepared/latch/retired counters, three FREE GEM slots and the READY blank
slot. These cached observations validate prerequisites; they grant no new
ownership or permission to access hardware.

Build the single-source module with GCC 16, using the configured matching
kernel's preprocessing and warning flags. `EMBER_KERNEL_OBJDIR` is mandatory.
Use the normal cross-module build environment and a separate module object
directory. A bare generic DRM module build is not a matching kernel contract.

Load exactly once with these typed module properties:

- `scanout-mode`: string `cycle`.
- `expected-buildinfo`: the exact running kernel `buildinfo` string, with only
  its final newline removed.

A missing property, an unsupported mode or a build mismatch is refused before
allocation. There is no status-only mode that reports acceptance without a cycle.
Use the existing native cached diagnostic interfaces for observation.

## Accepted sequence and retention

The module allocates three distinct native BOs with 8294400 logical pixel bytes each.
The native allocator rounds each BO to the kernel page size: 8294400 bytes
with 4 KiB pages and 8323072 bytes with 64 KiB pages. The module clears the full
allocation, including padding. It fills
all 1920x1080 XRGB8888 pixels through their private CMA CPU mappings before any
native reader lease exists. The fixed red, green and blue checker patterns are
never modified after preparation. There is no userspace handle or writer.

For each BO, the module prepares the actual native consumer, checks object
identity, exercises one balanced `get`/`put` pair and submits the frame. It drops
its remaining software frame reference and constructor GEM reference after
successful submission. The native consumer retains its own live resources.

The module waits on cached native status with bounded `kpause` calls. A latch
requires the actual native latch count, the expected CURRENT slot and no pending
slot. Before the next submission, every former slot must be FREE, the native
worker must be idle and its retirement count must include the former BO.
That cleanup follows the owner's actual additional-frame retirement rule.
Elapsed time never establishes retirement. A timeout fails the cycle.

Success requires exactly three preparations, three latches and two retired BOs.
The third BO stays CURRENT. The BO snapshot must show exactly three reservations,
two releases and one page-rounded BO of additional reserved bytes relative to the initial
snapshot. Other simultaneous BO allocations can make this strict accounting
fail; run the probe without other native BO consumers.

The success label requires a fresh final native status and BO snapshot.
A fault or inconsistent counter at that boundary changes the result to a
retained failure. The result prints the current and pending slots, all slot states, prepared,
latch and retired counters, worker state, caller references and exact BO snapshot
changes. A failed `put` is marked as reference-uncertain because its error does
not prove whether the native API consumed that software reference. The last
cached status and the latest status errno remain explicit.

A clean refusal before the first `prepare` releases only private allocations
and the render reference, then returns an errno. From the first `prepare`
attempt onward, the module remains loaded even if INIT returns 0 after an error.
It reports `NO SUCCESS; retained fault` and preserves every remaining reference.
`modunload` returns `EBUSY` after any attempt. Return 0 from INIT therefore does
not establish acceptance. The only accepted result is `cycle accepted` with the
exact counters above. The native owners, module, render reference and final
current BO have boot lifetime. Reboot is the only end of this probe session.

## Reproduce the host contract

```sh
ruby ember/tools/bcmv3d-scanout-probe-contract.rb
CC=cc CFLAGS='-O 1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  SCANOUT_PROBE_SKIP_MUTANTS=1 ruby ember/tools/bcmv3d-scanout-probe-contract.rb
```

The contract compiles the actual module body, actual flip body and their public
status types. Controlled services implement the kernel, BO and native owner
boundaries. The target GCC 16 TU compile checks the complete real header contract
separately. Every scenario runs with 4 KiB and 64 KiB pages. Tests cover exact-build binding, native device/ACPI identity, KMS
refusal, pristine owner checks, all three allocation/preparation/submission
positions, frame reference errors, cached-status errors, missing real latch,
missing additional-frame retirement, inconsistent counters and strict BO
accounting. It checks retention and refusal to unload after every attempted
cycle. A test-owned fixture reboot frees simulated retained resources only
between isolated boots; the module has no equivalent cleanup path.

`SCANOUT_PROBE_EVIDENCE` requires a fresh directory and keeps source hashes,
commands and outputs. `CC`, `CFLAGS` and `TEST_RUNNER` select the host toolchain
and execution wrapper. The default run also compiles causal source controls;
each must exit through a failed `CHECK`. A crash or timeout is not a valid
negative result. `SCANOUT_PROBE_SKIP_MUTANTS=1` selects the positive memory run.
