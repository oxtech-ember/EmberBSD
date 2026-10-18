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

One CM5 Rev1.0 with BCM2712D0 passed one explicit EDID0 load and unload.
The kernel source was `6e548fea09ffc04cb844c0d55c799ce25173a74b`.
The module source was `e44bb120e36bd5d65b588606eb51e8f9828e61f9`.
The firmware source was `c90c2a6cee52208fbc1fbe794a72c597f2bd9fd3`.
All selected source dependencies, generated headers, sampled ABI values and
strong symbols were checked before loading. Compatibility checks remained on.

The operator performed a cold boot without HDMI, then connected a powered
monitor to HDMI0 without rebooting. No firmware framebuffer or wsdisplay was
present. The provider completed five commands and 1195 polls. Native fault,
core fault and active were zero. Cached BSC status was zero; its mask was
`0xf`. Both cache-validity flags were set. No BSC mask or reset was written.

The EDID base block had a valid signature and checksum. Its SHA256 was
`b3db5c53386c0f5b46400a414c44c738dda5f494060c1f10ce4e4b94b1a44f5b`.
Its descriptor name was `PM1561`. The first detailed timing declared 1920 by
1080 at 60 Hz with a 148.5 MHz pixel clock. It declared one extension block;
this probe did not read that block. No independent monitor reference was
available, so an exact sink-reference comparison remains untested.

The DDC owner and mappings were released before DVP and HSM. DVP mutation
remained zero. HSM completed its 0 to 120 MHz request and released its lease.
Only then did the payload appear. The module unloaded successfully. The
reported temperature was 49.370 C before and after the transfer, then
48.821 C after unload. No throttling or undervoltage was reported.

This accepts one finite base-block transfer on this configuration. It does
not accept other monitors, extensions, disconnect during transfer, IRQ event
delivery, PHY programming, modesetting, scanout or a physical Plasma session.
