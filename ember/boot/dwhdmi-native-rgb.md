# Checked DW-HDMI RGB path

The existing DW-HDMI core accepts optional checked I/O callbacks.
The default byte and word bus-space paths remain available.
This change does not attach a new controller or start output on a board.

Set both `sc_read` and `sc_write` before `dwhdmi_attach`.
Each callback returns zero or an errno.
The first error is retained in `sc_io_error`.
Later core reads return zero without accessing hardware.
Later core writes return without accessing hardware.
There is no error reset or automatic retry.
DDC returns the retained error before its polling delay.

The platform must serialize calls.
It must reserve and retain its clocks, power, PHY and output owner before the first write.
Callbacks must check the register aperture and access width.
These callbacks do not prove firmware exclusion or bound a stalled bus transaction.

## Native RGB calls

`dwhdmi_native_prepare` requires the checked callbacks and an external PHY.
It accepts progressive RGB888 with no repetition or scan transform.
The initial bound is 1920 by 1080 at up to 165 MHz.
The normal and CRTC timing fields must agree exactly.
The function rejects blanking fields that the frame composer would truncate.
The platform must first admit the exact clock and mode from EDID.

The function uses the existing packetizer, frame composer and sampler.
It selects DVI-compatible video signaling on the HDMI connector.
It does not send HDMI AVI packets, audio or HDCP data.
It does not configure a PHY, enable output clocks or start a TCON.
It marks preparation complete only after every I/O operation succeeds.

`dwhdmi_native_start` requires successful preparation.
It starts only the pixel and TMDS clocks.
It keeps the audio, CEC, HDCP, CSC and repetition clocks disabled.
It does not start a TCON or prove that a sink shows the image.
Repeated prepare or start calls return `EALREADY`.
An I/O error retains the first failure and prevents further I/O.

The legacy DRM bridge keeps its existing clock behavior and PHY callbacks.
The new native path does not call the legacy `void` PHY hooks.
The platform must check its PHY operations separately.
There is no native teardown API in this bounded first profile.
Keep resources and any published scanout buffer until a proven stop or reboot.

## Source checks

Run the contract from the repository root:

```sh
python3 -B ember/tools/dwhdmi-native-contract.py
```

Set `CC`, `DWHDMI_CFLAGS` or `TEST_RUNNER` for host checks.
`DWHDMI_SOURCE_ROOT` selects a source tree for causal controls.
The runner extracts unchanged production functions and register constants.
The test supplies small host types and a deterministic I/O model.
Actual kernel translation-unit checks use the complete native headers.

The tests check byte and word default accesses, the RGB register sequence,
invalid modes, DDC bytes, failure at each I/O operation and error retention.
They check that a failed preparation or start cannot publish a success state.
The checks remain active with `-DNDEBUG`.
They do not establish physical PHY, DDC, HDMI, concurrency or DMA behavior.
ASan/UBSan host checks are not Valgrind acceptance.

The code retains the existing BSD license and authorship.
The checked path and tests are AI-assisted EmberBSD additions.
See the [A733 output contract](a733-output-timing.md) for its consumer boundary.
