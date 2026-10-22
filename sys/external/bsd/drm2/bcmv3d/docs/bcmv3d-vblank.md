# Native PV0 frame events

The PV0 event core observes VFP_START and acknowledges only that W1C source.
It reads actual HVS channel status, active display-list head and six-bit frame
count. An absent source does not advance its sequence. No timer creates events.
The mechanism does not establish BO retirement or send DRM page-flip events.

The native owner starts only after successful serial CPU close, firmware ACK,
both pipeline stops, MMU4 programming, fixed HVS publication and unused-range
checks. It borrows retained canonical mappings and clock lifetimes. It does
not reopen the boot owner's one-shot execution flag. The pinned Raspberry Pi
Linux `a1a601d4857978d810175365550c4536701df4ed` ARM64 `bcm2712.dtsi` declares
PV0 SPI101 (GSI133), display SPI97 (GSI129) and HVS EOF0 bit2.

Prepare verifies the initial running HVS head32. It masks inherited supported
PV0 sources, verifies INTEN0 and clears VFP before native IRQ establishment. A spurious interrupt before establishment
returns its handle performs no display I/O. Arm runs under the native IRQ mutex
only after the actual handle exists. An uncertain display I/O stops later
I/O and masks that real parent IRQ. The owner and resources remain until reboot.

`hw.bcmv3d0.display_irq` returns version1 of the 80-byte cached snapshot. It
contains actual sequence/spurious counts, preparation/arm/route state, first
error, last HVS data and the exact failing register/mask when available. Reading it performs no MMIO. Diagnostic creation
failure preserves the existing BO diagnostic. It is not a stable user API.

ARM64 IRQ access uses the retained native mappings directly. The process-only
onfault peek/poke mechanism is not used from the ISR. Native bus aborts or
stalls are not recoverable. Retained clocks and mapping ownership are required.

A future DRM CRTC can bind one interrupt-safe callback. The callback runs
outside the native mutex. It must copy the snapshot before returning if it
needs the data later. It must retain its text and argument until reboot.
A nonzero first_error callback reports a route fault, not a frame.
Prior events are not replayed. Suspend, detach and resource release are absent.

Run the actual-source contracts:

```sh
ruby ember/tools/bcmv3d-vblank-contract.rb
ruby ember/tools/bcmv3d-vblank-native-contract.rb
ruby ember/tools/bootfb-serial-handoff-contract.rb
```

Core tests cover both HVS variants and all prepare/arm/IRQ I/O failure positions.
Native tests execute the constructor, ISR, snapshot and bind bodies with
controlled kernel services. They cover early spurious dispatch, exact resource
identity, lock order, callbacks outside the mutex, sticky faults and retention.
Eleven native source controls and six core controls compile before a CHECK failure must reject them.
The boot contract executes the actual admission and Giant release around create.
These fixtures do not prove electrical IRQ delivery, frame rate or physical KMS.

`CC`, `CFLAGS`, `TEST_RUNNER`, `VBLANK_COMPILE_ONLY` and
`VBLANK_NATIVE_COMPILE_ONLY` select cross builds. `VBLANK_EVIDENCE` and
`VBLANK_NATIVE_EVIDENCE` preserve commands and inputs. Core sanitizer runs use `VBLANK_SKIP_MUTANTS=1`. Native sanitizer runs use
`VBLANK_NATIVE_SKIP_MUTANTS=1`. Actual hardware acceptance is separate.
