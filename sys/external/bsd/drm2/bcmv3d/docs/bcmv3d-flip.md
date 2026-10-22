# HVS list and frame protocol

The internal flip core tracks three GEM slots and one reserved empty list.
It consumes real PV0 snapshots. It does not allocate buffers, access MMIO,
publish a list, acquire fences or establish ownership of a hardware pool.
The [native HVS owner](bcmv3d-scanout.md) uses this protocol. DRM/KMS
integration and physical mutable-frame acceptance remain separate.

Initial head32 remains pinned. GEM heads42/52/62 use separate IOVA and UPM
ranges. Empty head72 has no GEM backing. The native owner must reserve these
resources before use. These numbers are not evidence that a pool is free.

A prepared slot becomes pending when the native owner starts publication.
The core reports a latch only when an actual later snapshot has that active
head. A programmed pointer is insufficient. The previous current slot then
retires after subsequent HVS frame-count progression. The worker must claim
that retired slot, complete actual DMA/map cleanup and end the read lease
before it reports reclamation. A timeout or a disabled-channel bit cannot
authorize reclamation.

The six-bit HVS count is unwrapped with a half-range bound. A delta of32 or
more, or an IRQ sequence gap over31, faults the protocol. A stale callback
cannot rewind the sequence. A route fault retains every hardware resource.
There is no reset, copy or recovery API. This is an HVS frame protocol,
not independent measurement of AXI drain. Numeric facts and the extra-frame
retirement rule were checked against Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`, `vc4_hvs.c` and `vc4_crtc.c`.

Recommitting the current BO waits for another actual frame event. It keeps
the current slot and read lifetime. It cannot replace that pending read fence
with a new fence that could be cancelled while scanout still uses the BO.

Run the actual-source contract:

```sh
ruby ember/tools/bcmv3d-flip-contract.rb
```

The contract checks4096 switches across counter wraps, unchanged frame counts,
late callbacks, same-BO commits, empty-list transitions, slot reuse and fault
retention. Twelve compiled source controls must fail a CHECK.
`CC`, `CFLAGS`, `TEST_RUNNER`, `FLIP_COMPILE_ONLY`, `FLIP_OUTPUT` and
`FLIP_EVIDENCE` select and preserve cross builds. `FLIP_SKIP_MUTANTS=1` selects
the positive sanitizer run. Compile calls have a180-second bound. Executable
calls have a30-second bound. A cross compile does not execute the target test.

[PV0 events](bcmv3d-vblank.md), [consumer DMA](bcmv3d-mmu4-range.md) and
the native hardware owner have separate contracts. Passing this source
contract does not establish mutable HVS fetch, BO retirement, KMS or Plasma.
