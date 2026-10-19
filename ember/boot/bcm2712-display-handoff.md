# Explicit firmware display notification

The BSD helper sends one fixed `NOTIFY_DISPLAY_DONE` property transaction.
The explicit `EMBERV3DCONSOLE` profile calls it from the same native boot owner
after successful complete CPU serial close and drain. Default profiles do not
invoke it. It adds no MMIO or power operation and changes no read-only HVS observer.
An ACK grants no inherited-state or display ownership.

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

The private native call checks its actual boot softc and CLOSED owner, then the
exact simplefb complete/error-free/closed/drained terminal record. MI validates
its own issued CLOSED lease and the same held serial route. A raw CPU close,
foreign lease, status failure or lost hold refuses before mailbox I/O. The owner
retains fresh attempted/complete/fault fields for this boot under Giant. Failed
gates retain a terminal fault without an attempted transaction. Re-entry and later
jobs issue no new request. The first owner error survives all later calls.

The fixed native transport supplies channel `BCMMBOX_CHANARM2VC` to
`bcmmbox_request`. It exposes no transport registry, caller state or MMIO right.
The existing mailbox implementation copies the stack packet into its own stable
DMA allocation. Timeout or unknown completion quarantines that allocation and
channel until reboot. A firmware failure leaves the CPU owner CLOSED with its
serial route retained; it cannot cancel, fall back or admit hardware operations.

Any future hardware owner must separately exclude conflicting display clients,
then read fresh controller state under its own explicit clock/cache/MMU lifetime.
Earlier snapshots become stale. A nonzero global HVS enable bit alone does not
describe active channels or transfer ownership. This stage performs no clocks,
ICAC, MMU4, HVS, PHY, scanout DMA or KMS operation. Native linkage and physical
acceptance of this notification stage remain pending.

Run `sh ember/tools/bcm2712-display-handoff-contract.sh`. The fixture compiles
the real C source, checks independent packet bytes, every one-bit reply
corruption, unsupported/error precedence, transport errors, re-entry and zero
post-attempt I/O. CHECK remains fatal with `NDEBUG`. Ten compiled behavioral
mutants verify important guards. `CC`, `CFLAGS`, `TEST_RUNNER`, `HANDOFF_OUTPUT`,
`HANDOFF_COMPILE_ONLY=1`, `HANDOFF_SKIP_MUTANTS=1` and `HANDOFF_EVIDENCE` support
cross compilation and evidence. Use common GCC16 and an EmberBSD target sysroot.
The [joined native contract](bootfb-serial-handoff.md) composes the actual caller,
fixed transport and this helper with controlled mailbox services.

Pinned primary facts:

- [Notification before display component binding](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_drv.c#L391-L405).
- [Tag identity](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/include/soc/bcm2835/raspberrypi-firmware.h#L100).
- [Zero-payload property construction](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/firmware/raspberrypi.c#L154-L184).
- [Mailbox format and ignored unknown tags](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface).

Only protocol facts inform this independent implementation. Linux warns after
notification failure and continues. This BSD integration requires
validated success. The source contract is stricter than that Linux policy.
