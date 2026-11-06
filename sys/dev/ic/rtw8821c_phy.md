# RTL8821C PHY

This native adaptation uses the BSD-3-Clause option of FreeBSD rtw88 at
`a143197801c680b958ab662c88cf086668a1958b`; the imported table source is
checked by `ember/tools/import-rtw8821c-table.py`. RF-A writes use SIPI 0xc90,
while RF-A reads use the 0x2800 window. Supported RFE values are 0, 2, 4 and
6. A caller serializes hardware access and must unwind a partially written
transaction on failure.

The current channel and power operations admit only 2.4 GHz channels 1–13 at
20 MHz with primary equal to center. They reject SRRC filter selection and
missing calibration or regulatory limits. Power programs legacy and HT 1SS
indices; it does not itself admit any transmit rate. The caller controls
regulatory and net80211 policy, qualification of the completed channel, and
whether a station may transmit.

IQK sends a 32-byte packet through the supplied serialized command callback;
the callback owns H2C sequence assignment. The 20 ms wait callback must sleep
and return an error on interruption. IQK requires the firmware completion
marker and a clear failure mask. The runtime decides when IQK is needed and
must not treat a failed or cancelled calibration as transmit readiness.

`ember/tools/rtw8821c-phy-contract.sh` accepts the selected native compiler as
`CC` and optional sanitizer flags in `RTW88_TEST_CFLAGS`. Its register traces
include reference results from the pinned donor functions; the channel and
power trace generators accept an explicit directory containing the pinned
donor files. Tests use in-memory bus operations and do not probe hardware.
