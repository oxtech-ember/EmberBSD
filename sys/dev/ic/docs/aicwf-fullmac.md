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

The DC version response confirms that the command service answered. The DC
transport must complete its revision-specific startup and RF setup before it
registers a network interface. The common station leaves the D80 firmware
extensions disabled on DC. Physical scan, association and IP transfer each
require a separate observation.

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
