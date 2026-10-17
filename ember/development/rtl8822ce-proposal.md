# Native RTL8822CE station driver proposal

Status: staged implementation. Physical station acceptance is pending.

## Problem and target

An Orange Pi 6 Plus with CIX P1 CD8180, 32 GB RAM and board ID
A4 (PHECDA-Rev A) exposes a Realtek RTL8822CE PCI function, `10ec:c822`.
The initial diagnostic kernel reported that function as not configured.
The existing `rtwn` driver targets different Realtek generations.

The delivered firmware is `BiosV1.5-W20260706-e321056a`.
An experimental EmberBSD diagnostic build reached a PL011 console and a
RAM root. A short bidirectional ICMP test passed through one RTL8126.
Those observations do not establish Wi-Fi, internal storage or full board support.
The console currently needs a temporary correction of the firmware SPCR
from UART3 to the UART2 exposed by DEBUG and DBG2.
The opt-in [loader correction](../boot/cix-spcr.md) is a separate boot
dependency. See the [current board evidence](../boards/orange-pi-6-plus.md).

## Scope and baseline

The accepting owner is the OS repository. Start from source
`93b4fe37fb25c8dbf571953722434695a923e86f`.
The physical diagnostic baseline was the experimental local revision
`7c29a5ba7172b5e43417919ec7bcb5b6769de5ee`.
That revision is not a published release or the implementation baseline.

Add a native PCI attachment, bounded DMA rings, RTL8822C hardware operations
and station integration with the existing `net80211` stack.
Use the existing firmware loader, `wpa_supplicant` and DHCP interfaces.
Do not introduce a parallel wireless stack or LinuxKPI WLAN dependency.
The primary code owners are `sys/dev/ic`, `sys/dev/pci`, the selected kernel
configuration and focused contracts under `ember/tools`.

Inspect the Realtek `rtw88` implementation at FreeBSD source revision
`a143197801c680b958ab662c88cf086668a1958b` as prior art.
Record each imported file's license and revision. GPL-only implementation
is protocol reference only. Verify the separate firmware asset's license,
origin, size and SHA256 before distributing it through an image profile.
No radio firmware binary is added to this proposal.

## Current source boundary

The native source includes firmware validation, PCI DMA rings, RTL8822C
register tables and bounded frame transfer. The core exposes copied-frame
callbacks for a native station wrapper. It rejects CRC/ICV errors and stale
DMA tags. The RX callback excludes the FCS and expires when it returns.
A DMA TX completion does not prove an acknowledgement from an access point.

The legacy port0 helper programs the local address, BSSID, AID, basic rates,
slot time and software-only crypto. It returns hardware and mailbox errors
before the caller can publish a successful station state. Frames use a
band-valid basic rate and retain software data sequence numbers.
Startup applies the Wi-Fi-only antenna grants and calibrated 20 MHz TX
power limits. It sends the general-info and PHYDM packet commands through
the H2C DMA ring before publishing started state. Runtime retuning pauses
TX and restores it only after both channel and power setup succeed.

Cold startup runs the native ADCK/DACK helper before HCI starts. Its bounded
noise filter computes a trimmed mean after at most 101 replacement passes.
A spread above five only prevents early exit. ADC/DAC residuals must still
converge below five within ten attempts. Incomplete or out-of-range samples,
readiness timeouts and bus failures stop startup and require a power reset.
Pinned-source trace tests include persistent noise; physical calibration
and station acceptance remain pending.

The kernel configuration selects the native station wrapper and ingress
generation policy. Use the standard interface tools when the PCI wrapper
attaches it. Legacy diagnostic writes return EBUSY while net80211 owns the
device. RSSI, thermal power tracking and higher-rate modes remain outside
this profile. Source contracts do not establish a linked wireless network.

The read-only PCI statistics expose direct RF18 A/B windows and numeric RX
handoff counters. Native beacon, channel and element-drop counters are
snapshots from the most recent net80211 input. The diagnostic path reads
that copy without entering the network stack. It does not print SSIDs or
BSSIDs, retune the radio or change frame admission.

Run the actual-source contracts with GCC16:

```sh
CC=gcc-16 sh ember/tools/rtw88-contract.sh
CC=gcc-16 sh ember/tools/rtw88-pci-contract.sh
CC=gcc-16 sh ember/tools/rtw88-start-contract.sh
CC=gcc-16 sh ember/tools/rtw88-fw-init-contract.sh
CC=gcc-16 sh ember/tools/rtw88-net-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-txpwr-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-coex-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-dack-contract.sh
```

Both 2.4 GHz and 5 GHz need physical station tests. Association, WPA2,
DHCP, bidirectional data transfer and reconnect recovery remain unaccepted.

## First acceptance scenario

Use GCC16 cross builds on the development host and the physical board for
radio execution. Keep the kernel, modules and firmware manifest coherent.

1. Identify the actual silicon revision and calibration data.
2. Validate firmware sections and DMA addresses before enabling transfers.
3. Scan permitted channels and associate with a controlled WPA2-Personal AP.
4. Obtain a DHCP address and transfer 16 MiB in each direction with matching SHA256.
5. Complete three reconnect cycles and a ten-minute continuous workload.
6. Detect AP loss, report bounded failures and recover without reboot.
7. Repeat a cold boot using the same verified artifacts.

The transfer size and duration are proposed acceptance criteria, not results.
WPA3, HT/VHT aggregation, AP mode, monitor mode and power management are
outside the first station scenario. Higher rates and additional bands need
separate evidence. Do not infer them from the chip data sheet.

## Validation and recovery

Test firmware and efuse bounds, descriptor codecs, address overflow,
resource failure cleanup and stale completion handling before integration.
Add executable causal regressions, cross-build checks and applicable static
analysis. Contracts and VM execution do not establish physical DMA or radio timing.
The board's 32 GB memory configuration requires an explicit DMA address
contract. Never silently truncate an address or disable the IOMMU.

Retain the accepted diagnostic SD image and coherent source manifests.
Use only the selected disposable external SD. Do not write internal storage
or replace the stock board firmware as part of this driver proposal.
Publish the tested capability boundaries after independent review and the
physical station scenario. Existing DDR training and ACPI observations
remain separate from Wi-Fi diagnosis.

Follow [CONTRIBUTING](../../CONTRIBUTING.md), the
[kernel guide](kernel.md) and the [board worksheet](board-change.md).
