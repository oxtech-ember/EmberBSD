# A733 native HDMI provider

The native provider connects the existing DISPLAY coordinator to checked
CCU, PHY, DW-HDMI and TV0 mechanisms.
It uses the separate `sun60ihdmi` device in `GENERIC64`.
The device shares `dwhdmi_core` with the existing DW-HDMI attachments.
The common `dwhdmi` device does not select A733 providers in ARM32 kernels.
Attach maps three fixed apertures and registers the output provider.
It does not read or write output registers or enable clocks and power.
The DE coordinator must retain its DISPLAY cookie before `get_mode`.
All output calls use that same cookie and native CLK_DE0 handle.

## First binding profile

The compatible string is `allwinner,sun60i-a733-hdmi`.
Use these fixed apertures and names:

| `reg-names` | Base | Size |
| --- | --- | --- |
| `hdmi` | 0x05520000 | 0x100000 |
| `vo1` | 0x05510000 | 0x1000 |
| `tv0` | 0x05730000 | 0x1000 |

Set `clocks` to native CLK_DE0 and `clock-names` to `de0`.
Set `power-domains` to the A733 PCK600 VO1 domain10.
Add `netbsd,consumer-managed-power` to prevent automatic power changes.
Set `netbsd,dcxo-source` to the native A733 RTC at 0x07090000.
Set `syscon` to the shared SYS_CFG window at 0x03000000, size0x1000.
Add `allwinner,external-resistor` only for the confirmed external resistor wiring.
The initial rail binding requires AXP8191 DCDC2 as `vdd08-supply` and CLDO2 as
`vcc18-supply`.
The driver retains the acquired rail handles and borrows their enabled state.
It checks 800000 and 1800000 microvolts through the native regulator API.
It does not change a rail voltage or enable state.
A missing enabled rail requires a separate owned power preparation step.

The SoC nodes stay disabled by default.
The Zero 3W board supplies this complete binding and connects
`output = <&hdmi_output>` on its DE coordinator.
Attach registers the provider and coordinator. It does not start output.
The explicit DE stage still requires its strict memory and ownership proofs.
A device-tree node does not establish DMA readiness or visible HDMI.
Do not add assigned clocks, resets or automatic pin control to this profile.

The rail and external-resistor wiring comes from the
[pinned board source](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/arch/arm64/boot/dts/allwinner/sun60i-a733-orangepi-zero3w.dts).
The native binding uses no vendor control implementation.

## Explicit operation sequence

`get_mode` checks the retained lease, physical DCXO and both rails.
It runs the [HDMI clock phase](a733-output-clocks.md) and VO1 route.
It selects the external resistor through serialized SYS_CFG masked updates.
The updates set bit12 at 0x160 and clear bits31:24 at 0x164.
Other fields remain unchanged.
The generic syscon API provides no bus-fault return value.
These writes have mask readback checks; they do not claim trapped bus faults.
It then prepares HPD with the transmitter off and initializes checked DW-HDMI.

DDC counts use the freshly admitted HDMI SFR rate.
The rate is distinct from physical DCXO.
The pinned BSP formula is `ceil(rate_hz * time_ns / 1000000000)`.
High time is 9184ns; low time is 10204ns.
Verified SFR24MHz gives high221 and low245.
The driver reads one EDID base block and uses the quiet shared parser.
It selects the preferred exact supported mode, then searches other base-block modes.
It does not invent a mode or round a PLL rate.
The first profile accepts progressive RGB/DVI, up to 1920 by1080.
The [PHY plan](a733-hdmi-phy.md) must encode the exact physical DCXO and pixel rate.
EDID extensions, HDMI audio and HDCP are outside this profile.

`prepare` programs the exact PHY plan, runs the TV0 clock phase and sets the
[finite TV0 timing profile](a733-tv0-native.md).
The explicit vendor-zero depth experiment remains required.
It prepares the shared DW-HDMI RGB path and checks retained resources again.
`start` checks PHY lock and HPD, starts DW-HDMI and starts TV0.
`observed_vblank` checks retained resources, PHY state and a new acknowledged TV0 event.
An error retains the owner, mappings and attempted writes.
No automatic rollback or retry follows an error.

## Validation boundary

Run the kernel configuration contract with a built native `nbconfig`:

```sh
python3 -B ember/tools/evbarm-config-contract.py \
    --nbconfig "$NBCONFIG" --work "$PROBE_PARENT"
```

The test checks actual `GENERIC` and `EMBER64` source and attachment selection.
It also checks separate A733-only and legacy-only HDMI configurations.
The test does not compile or link an ARM32 kernel.

Run `python3 -B ember/tools/a733-hdmi-native-contract.py` for the native adapter contract.
Use `A733_HDMI_NATIVE_CFLAGS` to add native sanitizer flags.
The test compiles the actual adapter, timing encoder and fixed PLL-plan function.
It replaces external provider, bus and parser calls with bounded test doubles.
It checks binding ranges, rail refusal, ordering, exact DDC counts, EDID mode selection,
callback failures, readback rejection and no I/O after a retained fault.
The PHY, TV0, CCU, parser and DW-HDMI have separate actual-source contracts.
A target object and these host tests do not establish physical HDMI output.
The implementation is independent BSD code and is AI-assisted.

## Output register sample

The optional `sun60i_a733_output_sample()` API reads output registers under
the output mutex after `prepare`. It needs the same owner and no sticky fault.
It returns `EOPNOTSUPP` when the provider has no sample callback.
A sample error is returned to the caller. It does not latch an output fault.
The native provider checks the CCU lease and makes reads only. It does not
change the preparation cache or any register.

The record has version, valid bits and eight words: VO1 0x00, VO1 0x20,
TV0 0x00, TV0 0x04, TV0 0x40, TV0 0x90, TV0 0xfc, and TV0 0xfc again after
1 ms. Valid bit N means that word N was read. In TV_DEBUG (0xfc), bits 15:0
are the current output line (A733 User Manual V1.00, section 15.2.10).
Two different line values show that the TV0 timing generator runs.
Equal values do not prove that it stopped.

## Cached preparation diagnostics

The optional `sun60i_a733_output_cached_trace()` API copies one initialized
provider record under the existing output mutex. It does not check clocks,
rails, readiness or hardware. It remains available after a sticky fault.
The callback must only copy cache. It must not call a public output dispatcher.
A provider without the callback remains valid for normal output operations.

The record contains 13 native-endian `uint32_t` words (52 bytes):

- Version, valid bits, local prepare phase, local prepare result, done.
- Last read namespace, offset, value, read-valid flag.
- Last write namespace, offset, requested value, write-valid flag.

All words are zero before `prepare`. Version 1 identifies this layout.
Valid bit 0 means local prepare was entered. Bit 1 means its result is complete.
Done is 1 only after local prepare returns 0. Phases are 0 none,
1 entry readiness, 2 PHY preparation, 3 TV0 clock admission, 4 TV0 timing,
5 DW-HDMI preparation, 6 final readiness, and 7 local prepare complete.
A later DE error can set the generic output fault while this local record
still has result 0 and done 1. That state does not identify an HDMI failure.

Namespaces are 0 unknown, 1 HDMI CORE8, 2 HDMI TOP32, 3 TV0, and 4 VO1.
Offsets are local to that namespace. Each transport attempt clears its
valid flag. A completed read sets read-valid to 1. A completed write and its
existing write barrier set write-valid to 1. Failed reads leave value 0.
A write-valid flag does not prove a matching readback or hardware completion.
The last read and last write need not refer to the same register.
The callbacks record only operations already issued during local prepare.
They add no read, write, delay, barrier or recovery operation.

The DE coordinator exposes this record through its read-only `output_trace`
sysctl. These cached diagnostics and host contracts do not establish physical
HDMI output. The existing masks, time limits, ownership and retention apply.

## Shared transmitter stop

`dwhdmi_native_stop()` uses the existing DW-HDMI clock-disable helper.
It checks the seven clock-disable fields before clearing native started state.
It retains the prepared mode for `dwhdmi_native_start()`.
An I/O failure or readback mismatch preserves uncertain started state and
blocks further native writes. The platform must stop its TCON separately.
This shared mechanism does not release an external PHY or clock lease.
