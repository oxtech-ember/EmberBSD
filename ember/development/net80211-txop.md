# Native WME TXOP width and consumer limits

The native `wmeParams.wmep_txopLimit` retains the unsigned 16-bit wire value.
Its unit is 32 microseconds. Drivers must reject a complete unsupported set
before they issue a hardware or firmware operation. They must not truncate
or saturate a value to fit a smaller destination.

This changes the internal kernel ABI. Rebuild the kernel and its modules
together. Do not load a module built with the previous structure layout.
The public WME ioctl request layout is unchanged. Its signed `i_val` slot
holds the TXOP bit pattern; use an unsigned 16-bit conversion to read it.

## Consumer inventory

Limits below are in canonical wire units. A rejected set returns
`EOPNOTSUPP`. Validation checks all four ACs before the first operation.

| Source and consumer | Destination units and width | Maximum wire TXOP | Contract path |
| --- | --- | ---: | --- |
| `sys/dev/ic/ath.c`, `ath_wme_update` | HAL u32 microseconds, AR5211/AR5212 channel time 20 bits | 32767 | Actual callback and queue property producer; reject before HAL access |
| `sys/dev/ic/rt2860.c`, `rt2860_updateedca` | MAC raw units, eight bits; DMA raw units, 16 bits per AC | 255 | Actual callback; reject before any register write |
| `sys/dev/usb/if_run.c`, `run_updateedca` and deferred callback | Same MAC and DMA fields as rt2860 | 255 | Validate before enqueue; callback uses an immutable four-AC snapshot |
| `sys/dev/pci/if_iwi.c`, `iwi_wme_update` | Firmware u16 microseconds | 2047 | Actual callback and command; includes the unchanged CCK/OFDM default sets |
| `sys/dev/pci/if_iwn.c`, `iwn_wme_update` | Firmware u16 microseconds | 2047 | Actual callback and command |
| `sys/dev/pci/if_wpi.c`, `wpi_wme_update` | Firmware u16 microseconds | 2047 | Actual callback and command |
| `sys/dev/pci/if_iwm.c`, MAC context command | Firmware u16 microseconds | 2047 | Actual outer command and common fill; REMOVE omits QoS |
| `ember/development/iwx-be200/src/if_iwx.c`, MAC context command | Firmware u16 microseconds | 2047 | Actual outer command and common fill; REMOVE, monitor and MLD MAC paths have no EDCA payload |
| Same iwx source, MLD station/link commands | Firmware u16 microseconds | 2047 | Actual add/update, link fill and removal functions; validate only a QoS update, before any command |
| `sys/dev/pci/if_rtwn.c`, `rtwn_wme_update` | Register raw units, 16 bits | 65535 | Actual callback and register writes; shift an unsigned 32-bit operand |
| `sys/dev/usb/if_urtwn.c`, WME callback | Register raw units, 16 bits | 65535 | Actual deferred callback and register writes; unsigned shift |
| `sys/dev/ic/rt2661.c`, disabled WME callback | Packed raw units, 16 bits each | 65535 | Extracted function checks unsigned shifts; the production `#if 0` remains |

The `notyet_edca` athn function consumes a different, unavailable EDCA
structure. It does not consume `wmeParams` and remains disabled.
Native parameter defaults and copies in `ieee80211_proto.c` retain the field
without conversion. Wire parsing and emission use little-endian 16-bit values.

The ath register masks are in the retained Atheros HAL `ar5211reg.h` and
`ar5212reg.h`. Intel firmware headers document `edca_txop` in microseconds.
The retained rtwn header defines `R92C_EDCA_PARAM_TXOP_M = 0xffff0000`.
The Ralink low eight-bit MAC field is also documented in the primary
[Linux rt2800 register source](https://github.com/torvalds/linux/blob/a804c5742bff224d4cb4d25dd383554d598be32b/drivers/net/wireless/ralink/rt2x00/rt2800.h).
No Linux implementation is imported by this patch.
The inherited eight-bit canonical field also exists in the compared
[FreeBSD source](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211_proto.h).
This is an EmberBSD correction, not an upstream fix claim.

## Reproduce host contracts

```sh
CC=gcc-16 sh ember/tools/net80211-txop-contract.sh
CC=gcc-16 TXOP_TEST_CFLAGS='-O0 -g -fanalyzer' TXOP_ANALYZE=1 \
    sh ember/tools/net80211-txop-contract.sh
CC=gcc-16 TXOP_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/net80211-txop-contract.sh
```

The runner extracts complete native functions and the changed native types.
It models only kernel services, unrelated station/command fields and hardware
or firmware sinks. Intel AC payload types come from the firmware headers.
It checks 0, 255, 256, 2047, 2048, 32767, 32768 and 65535, every rejected AC,
wire and ioctl round trips, deferred snapshot ownership and teardown.
Strict UBSan checks the packed unsigned shifts. `TEST_RUNNER` applies to each
executable and propagates failure. CI runs Memcheck, source analyzers and
strict ASan/UBSan. Host checks do not replace full kernel compilation or
physical acceptance of each driver.

The shared WME update chain still ignores the int driver callback result.
Local rejection prevents unsupported hardware writes, but does not propagate
an error back to the initiating ioctl or association. That gap must be closed
before enabling native WME negotiation. This prerequisite does not enable
WME, HT, VHT, aggregation or any wrapper classifier.

The [STA negotiation prerequisite](net80211-wme.md) adds strict peer parameter
validation and offer lifetime without closing that callback/error contract or
the stale compatibility applied-table gap.
