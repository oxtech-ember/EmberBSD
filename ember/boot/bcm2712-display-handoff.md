# Explicit firmware display notification

The pure BSD helper sends one fixed `NOTIFY_DISPLAY_DONE` property transaction.
It has no native caller, default invocation, MMIO or power operation. It does
not change the read-only HVS observer or authorize an inherited-state handoff.

Use fresh zeroed status and serialize access. The helper marks an attempt
before calling the transport. A re-entrant or later attempt returns `EBUSY`
without I/O. Transport errors are retained unchanged. There is no retry, reset,
fallback or cleanup operation. Never reset or replace attempted status during
this boot. The callback must not retain the local packet after return on any
errno. Native mailbox transport must copy into its own stable DMA allocation;
quarantine applies to that allocation.

The packet is24 little-endian bytes: `[24,0,0x30066,0,0,0]` as six words.
Exact success is `[24,0x80000000,0x30066,0,0x80000000,0]`. An otherwise matching
successful reply with tag-response0 returns `EOPNOTSUPP`. Other malformed replies
return `EIO`. Success sets `complete`; it does not establish an AXI-drain or
shared-cache/MMU2/MMU5 exclusion predicate. Callback return time is not bounded
by this pure helper. Existing native mailbox lifetime rules remain required.

A future native owner must exclude conflicting framebuffer and display clients,
call this before new PHY/HVS mutations, then read fresh controller state under
its own explicit clock/cache/MMU lifetime. Earlier snapshots become stale.
A nonzero global HVS enable bit alone does not describe active channels or
transfer ownership. No physical acceptance follows from these source checks.

Run `sh ember/tools/bcm2712-display-handoff-contract.sh`. The fixture compiles
the real C source, checks independent packet bytes, every one-bit reply
corruption, unsupported/error precedence, transport errors, re-entry and zero
post-attempt I/O. CHECK remains fatal with `NDEBUG`. Ten compiled behavioral
mutants verify important guards. `CC`, `CFLAGS`, `TEST_RUNNER`, `HANDOFF_OUTPUT`,
`HANDOFF_COMPILE_ONLY=1`, `HANDOFF_SKIP_MUTANTS=1` and `HANDOFF_EVIDENCE` support
cross compilation and evidence. Use common GCC16 and an EmberBSD target sysroot.

Pinned primary facts:

- [Notification before display component binding](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_drv.c#L391-L405).
- [Tag identity](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/include/soc/bcm2835/raspberrypi-firmware.h#L100).
- [Zero-payload property construction](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/firmware/raspberrypi.c#L154-L184).
- [Mailbox format and ignored unknown tags](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface).

Only protocol facts inform this independent implementation. Linux warns after
notification failure and continues. This proposed BSD integration requires
validated success. The source contract is stricter than that Linux policy.
