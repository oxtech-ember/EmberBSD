# A733 native HDMI provider

The native provider connects the existing DISPLAY coordinator to checked
CCU, PHY, DW-HDMI and TV0 mechanisms.
It uses the existing `dwhdmi` configuration attachment.
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

No board is enabled by this source change.
A board integration must supply the complete binding and connect
`output = <&hdmi_output>` on its DE coordinator.
Do not add assigned clocks, resets or automatic pin control to this profile.

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

Run `python3 -B ember/tools/a733-hdmi-native-contract.py` for the native adapter contract.
Use `A733_HDMI_NATIVE_CFLAGS` to add native sanitizer flags.
The test compiles the actual adapter, timing encoder and fixed PLL-plan function.
It replaces external provider, bus and parser calls with bounded test doubles.
It checks binding ranges, rail refusal, ordering, exact DDC counts, EDID mode selection,
callback failures, readback rejection and no I/O after a retained fault.
The PHY, TV0, CCU, parser and DW-HDMI have separate actual-source contracts.
A target object and these host tests do not establish physical HDMI output.
The implementation is independent BSD code and is AI-assisted.
