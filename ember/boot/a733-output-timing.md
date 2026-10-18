# A733 TV0 timing and output dispatch

The timing core encodes progressive RGB888 with one pixel per clock.
It performs no I/O.
The native dispatch accepts one permanent output provider.
It returns `ENXIO` until that provider registers.
This source step does not attach HDMI, read EDID, enable a PHY or start scanout.
It has no physical HDMI acceptance.

## TV0 fields

The implementation uses the A733 User Manual V1.00, section 15.2.10.
The [source record](a733-display-observer.md#sources-and-license-boundary)
pins the manual revision and SHA256.
The code is an independent BSD-2-Clause implementation.
No GPL implementation is imported.

| TV0 offset | Encoded value |
| --- | --- |
| `0x98` BASIC1 | Twice the progressive vertical total |
| `0x9c` BASIC2 | Active width minus one, active height minus one |
| `0xa0` BASIC3 | Horizontal total minus one, horizontal back porch plus sync minus one |
| `0xa4` BASIC4 | Vertical back porch plus sync minus one |
| `0xa8` BASIC5 | Horizontal and vertical sync widths minus one |
| `0x88` polarity | Positive horizontal sync in bit 25; positive vertical sync in bit 24 |

The A733 map differs from the older TCON map.
BASIC4 does not contain the vertical total in its upper half.
GCTL bit 31 is reserved in the A733 manual.
The timing core does not set that bit.

The encoder rejects zero clocks, unordered timings and field overflow.
A valid encoded timing does not prove an available pixel clock or sink mode.
The output remains unchanged on error.
The 600 MHz upper bound is a hardware field/profile bound, not a mode admission.

GINT0 uses write-zero-to-clear status bits.
The acknowledgment requires both vblank enable bit 30 and pending bit 14.
It clears only vblank and writes one to every other documented W0C flag.
This preserves a line or PSR event that arrives after the snapshot.
The caller must serialize the read and write against native interrupt writers.
The helper does not observe an interrupt or prove a frame was displayed.

## Native provider contract

Use one coordinator cookie for CCU, PCK, DE and output calls.
Register the provider with `sun60i_a733_output_register` after attachment checks.
Keep its argument and callback table valid for the kernel lifetime.
No detach or release API is provided.

Call `sun60i_a733_output_get_mode(phandle, owner, &mode)` first.
The callback may issue DDC transfers.
It must hold the required resource leases before any write attempt.
It must return an exact sink-supported clock and timing.
The dispatch retains the owner and validates the timing before it publishes it.
The dispatch caches this mode for the owner.

Call `sun60i_a733_output_prepare(phandle, owner, &mode)` with that exact mode.
The callback must check HPD again.
It prepares output resources without starting scanout.
Call `sun60i_a733_output_start(phandle, owner)` after DE preparation.
The start operation must not wait for the first RCQ completion.
DE may need active output timing before it can accept the RCQ request.
A separate DE check must establish request and completion semantics.

Call `sun60i_a733_output_observed_vblank` to observe a new TV0 vblank.
`ENOENT` means no event and does not latch a failure.
A successful callback publishes the sequence number.
A failed callback leaves the caller's sequence unchanged.

All callbacks run under one sleep mutex.
Callbacks must not call the dispatch API recursively.
The provider must serialize its IRQ state separately.
A different owner receives `EBUSY`.
A provider error latches the first failure and invokes its fault callback once.
Later operations return that failure without new provider calls.
`sun60i_a733_output_fault` lets the coordinator latch an external failure.
The fault callback must retain resources and must not free a live scanout buffer.

## Checks and limits

Run the contract from the repository root:

```sh
python3 -B ember/tools/a733-output-contract.py
```

Set `CC`, `OUTPUT_CFLAGS` or `TEST_RUNNER` to select host checks.
`OUTPUT_SOURCE_ROOT` selects a source tree for causal mutant checks.
The runner executes the production C after removing only kernel include plumbing.
Its kernel mutex stand-ins test serialized call order, not concurrent execution.
Tests remain active with `-DNDEBUG`.

The checks cover independent 720p and 1080p values, field limits, error outputs,
late W0C events, provider absence, owner conflicts, exact mode admission,
operation order, failed callbacks and first-error retention.
ASan/UBSan on a supported host supplements the contracts.
It is not Valgrind, DMA, linked-kernel or physical HDMI acceptance.

The [checked DW-HDMI RGB path](dwhdmi-native-rgb.md) supplies controller I/O.
A native hardware provider still needs checked TV0/VO1 writes, A733 PHY control,
DDC/EDID, clock and supply admission, HPD, and a bounded vblank lifecycle.
The [display ownership boundary](a733-display-ownership.md) remains mandatory.
Do not select the older H3/A64 `sunxi_hdmiphy` for the A733 SNPS C422-0 PHY.
