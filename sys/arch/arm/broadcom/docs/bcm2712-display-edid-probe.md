# Finite EDID0 probe

`ddc-mode=edid0` requests one base block through the native DDC provider. It
uses the same selected-kernel, cold-headless, framebuffer, render, HSM and DVP
guards as `observe0`. It requires inherited gate bit3 clear, reset bit1 clear
and no DVP mutation before controller access. The modes remain separate;
`observe0` still performs exactly its five reads and sends no transfer.

The selected kernel must enable `BCM2712_DISPLAY_DDC`. The module includes
its real generated `opt_bcm2712_ddc.h`. Reconfigure the selected kernel before
building; an old object tree has no substitute header or weak-symbol fallback.
With the option off, `edid0` returns `EOPNOTSUPP` before resources or providers.
Invalid or conflicting properties still return `EINVAL`. Audit matching native
DDC source, generated headers and strong runtime symbols before physical use.

The module maps resource13 then resource14. It calls native create, one
128-byte `read_edid`, cached status and destroy. The provider owns the private
BSD I2C tag and kernel-resident callbacks. The module owns no I2C algorithm.
The native provider validates the base EDID header and checksum. It reads no
extension blocks. Its cached diagnostic includes native/core faults, active,
command/poll counts, mutation and BSC status/mask validity; it adds no MMIO.

A known error without a retained native owner, or a read error with confirmed
fault0 and inactive core, completes normal cleanup and returns that error.
This includes `EBADMSG` after successful transport with invalid EDID data.
An uncertain status, invalid cached BSC validity flag, native/core fault,
active transfer or failed destroy stops cleanup. The module retains the
remaining owners, mappings and clocks. A cleanup error
has priority over a deferred operation error. There is no retry or bus recovery.

Only complete transfer and cleanup success print the payload. Logs contain
`BCM2712_EDID0 begin length=128`, eight `BCM2712_EDID0 data xx:` rows with
16 lowercase hexadecimal bytes each, then `BCM2712_EDID0 end length=128`.
Row offsets run from `00` through `70`. The module writes no filesystem file.
Every failure withholds all payload lines, including failures after valid data.

The actual-source probe fixture builds both feature configurations. It checks
the real HSM/DVP providers and decoder, and controls only the DDC API boundary.
It checks exact payload/order, map ranges, held leases, every API/map failure,
late cleanup failures, clean data-error cleanup and retained uncertainty.
Separate actual-source contracts cover the native DDC provider and I2C core.
These checks do not establish a physical EDID read, bus freedom, ACPI power,
KMS output or a Plasma session. Native MMIO stalls and aborts remain outside
recoverable probe errors.

## Physical base-block boundary


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
One checksummed base block does not accept extension blocks, other monitors, exact sink-reference equality or disconnection during transfer.
