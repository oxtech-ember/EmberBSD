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

DC Ethernet TX uses one transfer on the first bulk OUT endpoint. The driver
opens this endpoint when a connected station in normal mode submits data.
The USB header counts the entire transfer. EAPOL requests a firmware
confirmation with a 30-bit host index and pads the transfer to four bytes.
A short transfer, a missing confirmation, or a negative acknowledgement
fails the packet. The driver does not replay an uncertain submission.
In a secured network, EAPOL can pass before key setup. Other packets require
the controlled-port confirmation for the current key generation. A queued
pairwise key change retires permission before its firmware command runs.
A successful connection request and a matching association indication establish
the policy for that connection. Editing the next profile does not change it.
The passive survey attachment keeps the data OUT endpoint closed.

## DC U02 normal startup

On the Lyra Zero W the USB adapter prepares the radio after the root file
system mounts. It reproduces the normal-mode path of the pinned factory
driver, which the factory build compiles with `CONFIG_START_FROM_BOOTROM`,
`CONFIG_DPD` and `CONFIG_FORCE_DPD_CALIB`:

1. It writes the WLAN ROM patch to `0x180000` in 512-byte block writes.
2. It reads the normal-mode setting pointers at `0x10164`. It writes the
   WLAN settings and the LDPC, AGC and TX gain tables there.
3. It writes the patch table descriptor and then each address/value row.
4. If the RF misc RAM marker is absent, it loads the calibration program at
   `0x130000` and calls it at `0x130009`. The marker must then be present.
5. It sets the watchdog control bit at `0x4010300c` and hands the boot ROM
   over with a dummy start of `0x150000`. Without this step the firmware
   stops on the first `MM_RESET` and leaves the USB bus.
6. It sends `MM_SET_STACK_START`, the TX power levels from the board file,
   the four gain tables and the 2.4 GHz RF calibration.

The kernel logs each completed stage. When every stage succeeds, the station
attaches in normal mode. The transport then accepts the station's MM, ME,
SCANU and SM messages. The debug task and the RF and stack setup messages
stay with the transport. A failure before the first write to the radio logs
the stage and attaches the passive survey. A later failure also stops the
transport, because the radio state is then unknown. The factory build does
not enable `CONFIG_POWER_LIMIT`, so the power limit file is verified but not
sent.

To keep the passive survey, create an empty file named `passive` in
`/libdata/firmware/aic8800dc` and restart. The file can be added on another
computer through the microSD FFS. The kernel then reports that the station
start is disabled and leaves the radio unchanged.

The firmware reports MFP in its feature word, so the station offers external
SAE and firmware PMF. Unlike D80, DC has no firmware BIP readback check. The
firmware's rejection of forged protected broadcast management frames has not
been tested.

The hardware acceptance run on the Lyra Zero W covered a cold boot from
microSD, WPA3-SAE with required PMF and a BIP management group cipher, a
DHCP lease, and SSH from a host by the board's mDNS name. Two earlier runs
of equivalent code stopped all interrupt service on CPU0 at the first data
transfer. The cause is not known.

## DC passive survey

When the normal startup fails, bring the interface up and request a scan.

```sh
ifconfig aicwf0 up
ifconfig aicwf0 list scan
```

The command waits for this interface and prints its cached results. A
completed scan with no networks produces no output and exits successfully.
After a failed survey, reading the results returns `EIO` until a scan
succeeds. The kernel logs the failed operation and error code. A firmware
refusal of `MM_ADD_IF` also logs the numeric firmware status.

The driver starts in manual roaming mode. A scan creates a temporary firmware
station interface, listens on the selected channels and removes the interface.
Every channel has the no-initiate-radiation flag and a zero transmit power
field. The interface advertises passive channels and no WPA capability.
WPA configuration and association return `EOPNOTSUPP`. The USB adapter opens
the command OUT pipe and both IN pipes. It never opens the data OUT pipe.
Both IN pipes can deliver firmware messages.

DC USB DATA records contain a 60-byte receive descriptor and the declared
payload. The next record starts immediately. Configuration records have a
four-byte header and their payload is padded to four bytes. The USB adapter
retains an incomplete record separately on each bulk IN endpoint after a
full 2048-byte transfer. A short transfer must contain the complete payload.
It may omit the last configuration record's trailing pad. The station receives
one complete record with the generation from its first transfer.

The driver waits up to four seconds for scan completion after the start
confirmation. A late completion retires its original scan. If the VIF creation
times out, further scans return `EBUSY` until the firmware confirms a
rejection or the driver removes the VIF named in a late success reply.
An invalid status, length or VIF in a decoded creation reply stops further
scans with `EIO`. A failed removal also stops further scans.
The DC command version confirms basic service. Association and data transfer
require the normal startup above.

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
