# AIC FullMAC station

The `aicwf` driver connects AIC firmware to net80211. The shared station owns
scan results, association, key state, and Ethernet RX and TX. The transport
owns device startup, framing, and the lifetime of each bus transfer.
The device name comes from the `aicwf` attachment. The first station is
`aicwf0`. [Station implementation](../aicwf.c) and [transport contract](../aicwfvar.h).

| Device | Transport and startup | Firmware contract |
| --- | --- | --- |
| AIC8800D80 | SDIO function 1. The adapter loads the selected WLAN image, applies the board settings, and checks FIFO credits and CRC. | The tested U02 version is `0x06090101`. Firmware and PHY capabilities govern 5 GHz, VHT and external SAE. The firmware BIP path has additional image and readback checks. |
| AIC8800DC U02 on Lyra Zero W | The WLAN driver claims interface 2 of composite USB device `a69c:88dc`. The radio accepts data on the first bulk OUT pipe and commands on the second. Both bulk IN pipes can deliver firmware messages. | A hardware run reported chip ID `0x07`, sub-ID `0x01`, and FullMAC version `0x06040301`. The board profile covers 2.4 GHz channels 1 through 11. The power limit is the lower of 15 dBm and the current net80211 limit. |

## DC passive survey

On the known U02 command version, bring the interface up and request a scan.

```sh
ifconfig aicwf0 up
ifconfig aicwf0 scan
```

The driver starts in manual roaming mode. A scan creates a temporary firmware
station interface, listens on the selected channels and removes the interface.
Every channel has the no-initiate-radiation flag and a zero transmit power
field. The interface advertises passive channels and no WPA capability.
WPA configuration and association return `EOPNOTSUPP`. The USB adapter opens
the command OUT pipe and both IN pipes. It never opens the data OUT pipe.
Both IN pipes can deliver firmware messages.

The driver waits up to four seconds for scan completion after the start
confirmation. A late completion retires its original scan. If the VIF creation
times out, further scans return `EBUSY` until the firmware confirms a
rejection or the driver removes the VIF named in a late success reply.
An invalid status, length or VIF in a decoded creation reply stops further
scans with `EIO`. A failed removal also stops further scans.
The DC command version confirms basic service. Enabling association and
data transfer requires the revision-specific normal startup and RF setup.
Physical scan, association and IP transfer each require a separate observation.

The shared wire definitions are in [commands](../aicwf_command.h),
[station messages](../aicwf_sta.h), [SAE](../aicwf_sae.h) and
[capability encoding](../aicwf_vht.h). The D80 transport and its acceptance
records remain with [SDIO](../../sdmmc/if_aicwf.c),
[SAE](../../sdmmc/docs/aicwf-sae.md),
[firmware BIP](../../sdmmc/docs/aicwf-firmware-bip.md) and
[VHT](../../sdmmc/docs/aicwf-vht.md). The DC hardware profile and the pinned
normal-mode driver describe the board and device protocol.
[Lyra Zero W](https://github.com/oxtech-ember/EmberBSD-DevBoards/tree/main/boards/rk3506b-lyra-zero-w)
and [Luckfox SDK](https://github.com/EliSofke/LuckfoxLyraSDK/tree/6dcbca0d8a517d79bd8fb4205ca4966b136604bc/external/rkwifibt/drivers/aic8800/aic8800/aic8800_fdrv).
