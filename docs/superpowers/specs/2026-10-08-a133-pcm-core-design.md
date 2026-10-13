# A133 PCM transfer core

## Purpose and boundary

The tablet needs a normal NetBSD audio driver for its hotel voice application.
A bounded direct-register headphone-path probe produced audible output on one
YS-M33 sample. This is not an audio(4) driver or verified speech capture. This
milestone implements the reusable format and ring-buffer part of that driver;
it performs no MMIO, playback, recording, GPIO or supply changes.

The operator explicitly requested autonomous work without questions or audio
tests at night. Design and implementation proceed inline under that instruction.

## Hardware evidence and choices

Use Allwinner A133 User Manual revision 1.1, Audio register sections 9.4.6.3,
9.4.6.5 and 9.4.6.8. The existing sunxi_codec driver serves other SoCs and has
different ADC fields. Keep this A133 core separate so it cannot change those
devices. The eventual transfer mechanism (DMA versus FIFO IRQ) remains outside
this milestone; both can use the format and buffer geometry checks.

Initially allow signed little-endian 16-bit PCM at 16000 or 48000 Hz: mono
playback/capture and stereo playback. The NetBSD adapter will reject other
encodings and precision before calling this core. Do not advertise stereo
capture or 44.1 kHz until their clocking/routing is verified. The format words
select low 16-bit FIFO samples, TX threshold 64, RX threshold 32 and left ADC
only. They exclude enable, IRQ, DMA-request and flush bits. The observed 16 kHz
FIFO rate selector is 3, matching both FIFO rate tables in the manual and the
native and factory-Linux register observation.

## Contracts

`a133_pcm_format(mode, rate, channels, result)` returns zero for supported
formats and EINVAL otherwise. Mode is exactly PLAY or RECORD, not a bitset.
On failure, the caller's result remains unchanged; NULL is rejected.

`a133_pcm_ring_init(ring, size, block, frame)` accepts frame sizes 2 or 4 bytes,
two or more complete equal blocks, and block/frame alignment. Avoid computing
`2 * block`, which could overflow. Invalid input leaves the ring unchanged.

`a133_pcm_ring_chunk(ring, limit)` returns a frame-aligned byte count bounded by
the budget, current block boundary and buffer end. A zero-initialized inactive
ring returns zero. The caller copies that span before advancing it.

`a133_pcm_ring_advance(ring, bytes, completed)` accepts one nonempty aligned
span within the current block. It advances the offset, wraps at the buffer end
and reports one block completion. Invalid operations change neither ring nor
completion result. A completion means memory has been copied to/from the FIFO,
not that its samples have been heard. No heap allocation or callback occurs in
the core. The eventual driver serializes access using its audio interrupt lock.

## Verification and follow-up

Test the production source on macOS and native NetBSD/aarch64, with literal
register fixtures, buffer-wrap/canary tests, overflow and invalid input cases.
The signed-PCM encoding check, audio_hw_if, clock/GPIO ownership, interrupt
service, resource unwind and physical duplex/underrun acceptance are follow-up
driver work. Do not enable this core in the kernel configuration or replace a
working kernel as part of this milestone.
