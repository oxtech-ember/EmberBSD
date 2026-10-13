# Raspberry Pi Zero 2 W

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

This page preserves the published hardware results; it is not a new test run.
Board revision: not recorded in the original support table.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | BCM2710A1 |
| Boot path | Native firmware and device tree |
| Boot storage | microSD: Tested |
| Serial console | Tested |
| All CPU cores | Tested |
| Ethernet | No port |
| Wi-Fi | Tested at 2.4 GHz; large transfers stall |
| Bluetooth | Not validated |
| Temperature sensor | Tested |
| Fan control | No fan |
| Watchdog | Not validated |
| Power button | None on the board |
| I2C | Not validated |
| Audio | Not validated |
| Pin multiplexing and GPIO | Not validated |
| Real-time clock | None on the board |
| Processor frequency control | Not validated |
| Voltage regulators | None on the board |
| USB | Not validated |
| Hardware random numbers | Not validated |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Evidence

Results were migrated from the [published support matrix](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
