# A133 codec and PCM contracts

Hardware routing and audio limits belong to the [A133 profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/ys-m33-a133/interfaces.md).

Attachment maps resources and enumerates audio without preparing clocks,
enabling interrupts or starting streams. Both mixer controls default to
disabled: `outputs.route` accepts `speaker`, and `record.route` accepts `mic1`.
Only explicit route enable prepares hardware, in sleepable thread context;
running streams reject route changes. No playback or recording runs at boot.
The adapter checks buffer ownership, format, block size and route state before
trigger. Interrupt service transfers at most 128 samples per direction and
counts faults and underrun/overrun events. DMA and suspend/resume are not claimed.

The [register lease](../sun50i_a133_codec_io.c) restores
owned codec state, clocks and the PF6 GPIO while preserving unrelated pins.
It never asserts the codec bus reset: that would erase unowned bias, DAP and
calibration state. If it releases an initially held reset, reset deliberately
stays released after final disable; the original clock gates are restored.
A PLL timeout before release retains the original reset state. This is a
documented exception to full CCU-state restoration, not a hardware validation.

The [I/O contract](../../../../../ember/tools/a133-audio-io-test.sh) includes a reset-aware bank,
PLL timeout, calibration preservation and finite FIFO transfers. The
[adapter contract](../../../../../ember/tools/a133-audio-interface-test.sh) executes production
format, trigger/halt, interrupt and service functions: finite blocks, wrap,
signed capture words, flush timeout, impossible counts and no callbacks after
halt. Allocation/free and mixer IRQ establishment are reviewed and compiled;
their complete lifecycle is not executed by that portable fixture.

The [A133 PCM core](../sun50i_a133_pcm.c) validates mono
16-bit playback/capture and stereo playback at 16 or 48 kHz. It constructs
FIFO format words with stream, IRQ and DMA enables cleared. A caller must
validate signed little-endian encoding/precision, then supply the resulting
frame size when initializing a ring with at least two equal aligned blocks.
Copy each `a133_pcm_ring_chunk()` span before `a133_pcm_ring_advance()`;
serialize access under the driver's interrupt lock. Block completion
means the memory span has been transferred, not that the hardware has played it.

The [format/ring contract](../../../../../ember/tools/a133-pcm-test.sh) runs the production source
without hardware access. On native NetBSD, the
[kernel-context check](../../../../../ember/tools/a133-pcm-kernel-check.sh) accepts a source root
and a configured kernel compile directory, and compiles with its real flags.
These checks do not exercise the physical codec. The experimental configuration
selects the core and adapter; keep physical acceptance separate from software
contracts and the native kernel-context compilation check.


## Format and ring failure contract

`a133_pcm_format()` accepts exactly PLAY or RECORD. Combined mode bits, NULL,
unsupported rates and stereo capture return EINVAL without changing output.
`a133_pcm_ring_init()` accepts frame sizes 2 or 4 and checks `size / block >= 2`
after alignment checks, avoiding an overflowing `2 * block` expression.
Invalid initialization or advancement changes neither ring nor completion output.
An inactive ring yields a zero chunk. Advance exactly one nonempty, frame-aligned
span within the current block; no allocation or callback occurs in the core.
The kernel-context check covers the `_KERNEL` header path, including the NULL
definition supplied by `sys/param.h`.

## Adapter ownership and service

The FDT adapter requires the A133 identity, verified codec/machine/PIO resources,
PF6 route, active-low PA declaration, 120 ms settle and SPI25 level-high IRQ.
It clears stale resource markers before validation. Unknown or disabled resources
remain unmarked. Six-cell GPIO providers and unrelated consumers remain intact.
The audio driver also requires the marker and A133 root.

Preparation rejects active DAC/ADC state and non-codec PLL consumers. It takes
one clock lease and saves owned register fields before releasing reset. PLL lock
has a bounded 100 ms wait outside interrupt context. MIC1 bias settles for 200 ms.
No PMIC voltage write occurs. IRQ establishment follows preparation with FIFO and
headset IRQ enables cleared; failure unwinds the lease and leaves routes disabled.

Rings use ordinary kernel memory, at most 1 MiB. Allocation-list changes hold the
interrupt lock, while allocation and free run outside it. Active rings cannot be
freed. Blocks are frame-aligned and at least 1024 bytes; rings have at least two
blocks. Initial prefill is at most 256 bytes, so it emits no block callback.
Playback writes low16 little-endian left then right. Capture retains low16 even
if the RX register sign-extends the sample. A service operation uses one bounded
FIFO-availability snapshot and cannot cross a block boundary.

Callbacks run under the interrupt lock after the memory copy. Halt disables IRQ
and digital stream enables before clearing callbacks. Only normal FIFO and
underrun/overrun W1C bits are acknowledged. XRUNs increment counters before bounded
service continues; invalid stream state disables that direction. Closing an audio
file may leave its route prepared. Explicitly disabling the final route restores
the lease and detaches the IRQ. Child devices detach first; a busy child prevents
removal. Software duplex does not establish physical duplex or acoustic quality.

## Provenance

Contracts consolidated from these documents at OS revision `4a4c98bb99999836103d3fe63fd6f4c98013f0c5`.
Git retains their original text; the hashes identify the inputs.

- [2026-10-08-a133-pcm-core-design.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/specs/2026-10-08-a133-pcm-core-design.md): SHA256 `c5a57067f8b70fc2068bc85fb1a6ea8a8c3e302b297a2418858cbeea82773aca`.
- [2026-10-08-a133-audio-driver-design.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/specs/2026-10-08-a133-audio-driver-design.md): SHA256 `7d481ed3fd4cb82b1fd0d2f00416bda7c2d9d4e9f47e306bf5f421ae06250fcc`.
- [2026-10-08-a133-pcm-core.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/receipts/2026-10-08-a133-pcm-core.md): SHA256 `c91e2c578748e067587329855e2d68d1b2db3ac3f7cd0a91a6f4a31308a8eb35`.
