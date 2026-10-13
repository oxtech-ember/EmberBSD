# YS-M33 native audio(4) playback and bounded capture

Validation date: 2026-10-08, daytime physical tests on the same A133 sample.
This extends the [default-off attachment receipt](2026-10-08-a133-audio-driver.md).
Two standard-driver speaker tests were heard by the operator; a short MIC1
capture produced data, followed by a bounded concurrent playback/capture
test. Intelligible speech, latency and sustained streaming are not accepted
by this receipt.

## Installed configuration

Vendor firmware and the installed `EMBER64_A133_AUDIO #0` were retained.
Its clean native build source is
`811491a81414e8dcd720e13fa584ce1ccbc05d5e`; the final reviewed audio sources
in `d245249485d2` are identical. This session changed no driver or kernel.
Fresh `origin/main` was inspected while retaining the isolated source branch;
that published branch does not yet contain these A133 driver files.

The tablet booted after power was connected. Both routes started disabled,
with no active streams. Native `audiocfg list` reported 16-bit, 48 kHz stereo
hardware playback and 16-bit, 48 kHz mono hardware capture. The driver's
16/48 kHz mono and stereo-playback capabilities were enumerated separately.
The 16 kHz files below used the MI audio layer's format/rate conversion;
they do not establish native 16 kHz hardware operation.

## Audible playback

The synthetic WAV contained 32,000 signed 16-bit mono frames at 16 kHz:
a 1 kHz sine, peak 4096, with 20 ms edge ramps. Size was 64,044 bytes and
SHA-256 `32c06688b09641ea3bef3153315d467da3656009c679a0798a4e532e25d8cf0b`.
Before each test, the wrapper checked the board, file hash, idle streams and
disabled routes. It explicitly enabled `outputs.route=speaker`, ran
`audioplay -d /dev/audio0 -v 255` on that file, and disabled the route afterward.
A bounded process watchdog and exit cleanup were installed.

Both runs exited zero and the operator confirmed hearing each signal.
The driver's FIFO interrupt count rose from zero to 3048 after the first
run and to 6096 after the second. Playback and capture were inactive after
each run, with both routes disabled. No diagnostic MMIO program or direct
register writes were used by the test application.

## Bounded microphone capture

With speaker output off, the wrapper explicitly enabled `record.route=mic1`
and ran `audiorecord -d /dev/audio0 -c 1 -e slinear_le -P 16 -s 16000 -F wav`
with a nominal four-second limit and an outer process watchdog. It exited
zero, then disabled the record route. The private WAV was mode 0600;
its contents remain outside Git and were not sent to a speech service.

The parsed result was 65,536 frames, 4.096 seconds at 16 kHz, one channel,
16-bit PCM. The time limit ended at a userland buffer boundary. The waveform
had five clipped startup frames at indices 0, 1, 2, 3 and 5. After the first
100 ms, AC RMS was 59.6 full-scale integer units and peak magnitude was 533.
This is low-level capture evidence, not proof of intelligible speech.
An earlier direct-register recording also contains clipped startup samples;
the cause has not been isolated to the codec, stream setup or conversion.
Do not silently remove this limitation from an application acceptance claim.

The FIFO interrupt count rose from 6096 to 12054 during capture. The final
verbose event-counter query reported zero TX underruns, RX overruns and FIFO
faults. Open/sample fields returned by a separate control descriptor are
descriptor-local in NetBSD's `audiogetinfo`, so zero sample counts there do
not mean no frames were transferred. Global active flags, driver events,
successful completion and the WAV data provide the transfer evidence here.

## Bounded simultaneous playback and capture

A second 4.096-second recording ran with both routes explicitly enabled.
After one second, `audioplay` sent the same two-second synthetic waveform
while `audiorecord` remained active. Separate native `timeout` wrappers
bounded the processes. Both completed with exit zero; cleanup disabled
both routes and the final active flags were zero.

The recording contained 65,536 16 kHz mono frames with no clipped samples.
In 0.25-second windows before playback, the 1 kHz sinusoidal amplitude was
1.4–1.8 integer units. During the steady playback interval it was
1739–1749, with AC RMS 1251–1259; after playback it fell to 0.2–3.4.
The mic input therefore tracked the playback signal. This test does not
isolate acoustic pickup from possible electrical crosstalk or establish
spoken-word intelligibility, echo cancellation or speech quality.

FIFO interrupts rose from 12054 to 21044; verbose counters still reported
zero TX underruns, RX overruns and FIFO faults. These are native simultaneous
stream results on this sample, not sustained duplex acceptance. The second
recording had no clipped startup frames; no driver change was made and
the earlier startup transient remains unexplained.

## Spoken phrase acceptance

A subsequent nominal eight-second MIC1 recording contained 131,072 mono
frames at 16 kHz (8.192 seconds). It had one clipped startup sample. After
the first 100 ms, AC RMS was 191.1 and peak magnitude 1483, with DC offset
about -34.1. A raw-file local recognizer did not transcribe the phrase correctly.

A private listening derivative removed the first 100 ms and DC offset,
then applied 5.4x gain without clipping. The operator confirmed that playback
of this recording through the tablet speaker contained intelligible speech.
An offline Russian recognizer running on the tablet correctly transcribed
the repeated microphone-check phrase from this derivative. Original speech
and model/application configuration remain outside the OS repository.

The operator subsequently confirmed hearing a separately installed native
voice application's greeting and an answer to a spoken question. This is
one application's acceptance, not general speech-quality, latency or sustained
duplex validation. The startup transient and low raw level remain limitations.

## Retained state and remaining work

At the end of the bounded audio-only tests, routes and active flags were off. Xorg/awesomeWM
continued at 1280x800; Ethernet/SSH and MCU keepalive remained available.
The full 32 MiB eMMC boot wedge SHA-256 was still
`cb2ce60aceaa4ef31d76ee4a25eafa9653357509bb9de55a8b6630a5d2a95b72`.
No boot image, firmware, saved environment or PMIC supply setting was changed.

Remaining acceptance includes clean startup or an explicitly justified startup
policy, general microphone quality, sustained duplex and measured latency,
CPU use and XRUN rates. The short runs and one intelligible phrase are not
a sustained stability test. Automatic voice service remains outside the OS
driver acceptance; application configuration and credentials are deployed
separately. Later USB host kernel deployment is recorded in its own receipt.
