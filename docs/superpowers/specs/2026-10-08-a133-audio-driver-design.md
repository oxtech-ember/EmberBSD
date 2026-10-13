# YS-M33 A133 audio driver

## Intent and current boundary

Provide normal NetBSD audio(4) playback/capture for the tablet's hotel voice
application. The PCM core is already validated. The operator authorizes inline,
autonomous night work: no sound signals, microphone recording or questions.
Produce a compiled driver and boot artifact; retain kernel #7 until unattended
recovery through UART is verified. That recovery gate has now passed: software
reboot and serial countdown interception were verified before the reviewed
kernel was tested from RAM and installed in the backed-up boot wedge.
Physical acoustics, microphone quality and voice application acceptance remain
separate daytime checks. No Wi-Fi or custom U-Boot work is hidden in this driver.

## Approach

An isolated A133 driver avoids changing the ADC layout and fixed-format DMA
assumptions in the legacy sunxi_codec driver. Use bounded FIFO interrupt service
on SPI25 (GIC IRQ57), independently documented by the factory tree and A133
manual rev1.1. DMA integration is deferred until the A100 DMA route is verified.
PIO interrupt latency/CPU load and underruns require physical acceptance before
claiming stability. Initial formats are signed LE16: mono play/record and stereo
play at 16000/48000 Hz, as established by the existing core.

The FDT adapter marks only the verified legacy codec address 0x05096000/size
0x32c, machine phandle, PIO PF6 route, active-low pa_level0, settle120ms and SPI25
level-high interrupt. Unknown, disabled or mismatched resources are not marked.
Keep six-cell GPIO providers and unrelated consumers intact. The kernel driver
also requires the A133 root and marker. It maps codec/CCU/PIO, initializes locks
and attaches audio(4) without reading or writing hardware registers or enabling
an interrupt. Mixer routes default to disabled; get/query operations are silent.

## Preparation and resource ownership

Only explicit mixer route enable may prepare hardware. Preparation runs under
the audio thread lock, not the interrupt lock. It acquires the single codec
clock lease, saves owned register state, rejects an active DAC/ADC or non-codec
PLL consumers, and programs the measured 98.304MHz PLL (N39/P4/M0=0/M1=1,
pattern0 c001eb85). Module DAC/ADC clocks divide by4. Poll PLL lock with a bounded
100ms nominal wait, never in an ISR. Deassert codec bus reset with bounded delays.
Clock operations restore gates, PLL and owned fields on failure or final disable;
unrelated CCU fields, PIO pins, PMIC rails and calibration are preserved. One
intentional exception: after releasing codec bus reset, leave it released even
when returning the bus gate to its original state. Never pulse or reassert that
reset: it erases unowned BIAS/DAP/calibration state. A timeout before release
preserves the original reset bit. This is not full restoration of the CCU reset
state; the original clock gate state is restored and saved stream enables
were inactive.

Save codec state before releasing reset; disable headset interrupts and DSP DAP. The
speaker route follows the heard native HPOUT pattern DAC310=1b15d05a,
HP324 low16=8f8c with upper calibration preserved. PF6 low follows the inspected
vendor pa_level; polarity isolation is still pending. MIC1 uses ADC-left gain16
and bias bit7 with200ms settle. AVCC/CPVIN remain1.8V; never write PMIC voltage.
Idle route changes are serialized; any active stream makes a route change EBUSY.
Disabling the final route restores the lease and detaches its IRQ. A route may
remain prepared while audio files close; halt disables stream/IRQ bits, and
explicit route disable returns the hardware to saved state.

## Streaming and safety

Before trigger, verify the requested route is enabled, hardware prepared,
encoding/precision/rate/channels valid, and the ring lies within an allocation
owned by that direction. Invalid requests cause no register writes. Allocate
ordinary kernel memory, maximum1MiB; allocation-list changes use the interrupt
lock but memory allocation/free occur outside it. Never free an active ring.

Use frame-aligned blocks of at least1024bytes, ring>=2blocks. FIFO service reads
one bounded availability snapshot, transfers<=128samples and never crosses a
block per core operation. Playback is low16 little-endian left then right;
capture stores low16 regardless of sign-extension in the RX word. Driver
callbacks occur after the memory span is transferred, under the interrupt lock;
this is not exact hardware presentation timing. No callback occurs during
initial prefill because its<=256bytes is less than the minimum block.

Only normal FIFO and underrun/overrun bits are acknowledged (W1C). Count XRUNs,
then continue bounded refill/drain; no interrupt logging loop. Invalid stream
state disables that direction. Halt disables IRQ and digital enable before
clearing callbacks. IRQ establishment occurs after preparation with FIFO/headset
IRQ enables cleared; failure restores the lease and leaves routes disabled.
Independent playback/capture parameters and software duplex are implemented,
but physical duplex is not claimed. Detach first detaches audio children; busy
children prevent removal. No suspend/resume claim.

## Verification

Exercise production I/O helper through a finite fake register bank/FIFO because
physical audio is prohibited at night. Verify clock timeout/unwind, GPIO ownership,
calibration preservation, PCM byte order, bounded budgets, partial blocks,
wrap, route-off no-I/O and wrong formats/directions. This is software behavior,
not hardware acceptance. Build a clean pinned source natively as a separate A133
audio kernel config. Run the existing five contracts and independent review.
Prepare the Android boot container privately and verify it structurally.
Installation follows only after recovery, an off-device backup, a RAM boot
and complete partition readback have passed. The
[validation receipt](../../../ember/boards/validation/2026-10-08-a133-audio-driver.md)
records that completed default-off attachment milestone.
