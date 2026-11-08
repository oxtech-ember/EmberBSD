# WPA3-Personal consumers and evidence

This matrix separates the protocol implementation from physical acceptance.
Nick is RTL8821CE using native `rtw88`, as recorded in the product wiki's
[Nick receipt](https://github.com/oxtech-ember/ember-wiki/blob/main/wiki/product/dolgi.md).
RTL8852BE using `rtwx` is a different device. A chipset name or a capability
bit alone does not establish that a complete WPA3 path works.

The [DevBoards inventory at `0e8fa4018022`](https://github.com/oxtech-ember/EmberBSD-DevBoards/tree/0e8fa40180224dd49c64ccdfddbf4dc7e7cf3058/boards)
contains nine Wi-Fi profiles: Zero3W, Zero4, Zero2W, CM5, Pi 5, CIX P1,
A133, ESP32-S31 and ESP-Mosaico. PlutoSDR Nano is an SDR/USB-IIO profile.
The product wiki additionally records Nick, Jasper Lake and experimental
BE200. These cover five adopted provider families (`rtw88`, `rtwx`, `bwfm`,
`aicwf`, `espwifi`) and the experimental `iwx` module. Inherited NetBSD
drivers are not evidence of an adopted product or qualified WPA3 transport.

## Ownership

The BSD supplicant owns SAE authentication and EAPOL. The selected association
RSN profile is checked by common net80211: CCMP, SAE, mandatory PMF and
BIP-CMAC-128. A transition-mode AP may advertise both PSK and SAE; a selected
SAE association must still contain the strict SAE profile. The driver does
not silently fall back to PSK after SAE failure.

Raw native providers opt into `ieee80211_sae_attach()` after attaching software
crypto and their queue fences. Common net80211 owns authentication events,
association admission, PTK/GTK/IGTK lifetime, controlled-port authorization,
CCMP management authentication and BIP replay checks. Drivers own raw frame
transport and hardware state. They check tagged packet generations before
encryption/handoff and retire sessions synchronously before deferred stop or
reconfiguration. Ciphertext already submitted to DMA belongs to the transport;
no deferred plaintext may select a new session's key.
Native registration requires atomic 64-bit serial publication; unsupported
targets remain opted out, while their existing FullMAC contracts are unchanged.

FullMAC providers retain their firmware lifecycle and key installation. Their
capabilities must come from the actual firmware contract. They use the common
SAE ioctl authorization, RSN profile and station-management validators. The
native key-epoch extension is negotiated separately and does not reinterpret
the existing FullMAC key ABI.

## Model and driver matrix

| Product or board | Radio / provider | SAE and crypto ownership | Evidence and outstanding qualification |
| --- | --- | --- | --- |
| Nick | RTL8821CE / `rtw88` PCIe | Common native SAE, software CCMP/PMF/BIP | WPA2/CCMP 2.4 GHz reboot/reconnect, DHCP and bidirectional integrity accepted at OS `e9318f74c20ceb6b1011c8d18d9cdd6c1641ba27`; WPA3 software integration is new and requires hardware acceptance. |
| CIX board | RTL8822CE / `rtw88` PCIe | Same native protocol owner; separate chip calibration/transport | Existing WPA2 acceptance is not evidence for WPA3 PMF, rekey or reconnect. |
| A133 board | RTL8723DS / `rtw88` SDIO | Same native protocol owner; SDIO key hooks withdraw asynchronous encoded work | Profile-gated software attachment exists, but the board power profile remains disabled. The catalog records bootstrap only, without a network interface; queue/epoch tests do not establish radio acceptance. |
| Jasper Lake board | RTL8852BE / `rtwx` | Common native SAE and software crypto; rtw89 bridge submits ciphertext | Prior WPA2 association, DHCP, HTTPS and reassociation accepted at legacy rates; new WPA3 path awaits hardware. This is not Nick. |
| Pi 5 / CM5 | CYW43455 / `bwfm` | Host SAE/EAPOL; firmware association, CCMP and PMF/BIP | Existing SAE/H2E group 19, required-PMF data/reconnect/reboot evidence; CM5 wrong-password and WPA2 regression recorded. Forgery/replay, load and additional AP combinations remain separate acceptance. |
| Pi Zero 2 W | Broadcom / `bwfm` | Shared FullMAC adapter, capability checked per firmware | Sharing the driver does not inherit CM5 firmware or board acceptance. |
| Zero3W | AIC8800D80 / `aicwf` | Host SAE/EAPOL; firmware data crypto/BIP, host SA Query and peer/epoch checks | Existing group-19 SAE, PMF data, SA Query and a rekey case recorded. Initial nonzero IGTK IPN, reinstall/replay/forgery and WPA3 VHT80 load require further acceptance. |
| Zero4 / A733 | AIC8800D80 / `aicwf` | Same FullMAC protocol owner; separate board/firmware qualification | [WPA2/CCMP on both bands and HT40](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/0e8fa40180224dd49c64ccdfddbf4dc7e7cf3058/boards/a733-zero4/interfaces.md) recorded. Zero3W SAE and VHT80 results do not establish WPA3 acceptance on Zero4. |
| ESP32-S31 generic board | Espressif / `espwifi` | Host SAE/EAPOL; firmware association and key installation | [WPA2 and 6000-ping receipt](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/0e8fa40180224dd49c64ccdfddbf4dc7e7cf3058/boards/esp32-s31/interfaces.md) belongs to this profile; it does not qualify Mosaico or WPA3. |
| Mosaico / ESP32-S31 kit | Espressif / `espwifi` | Same FullMAC protocol owner; separate ROM/IDF integration | Active Mosaico owner controls ROM/IDF RX and firmware work. SAE/association completed; EAPOL reception failure also reproduces with WPA2. No completed WPA3 data-session claim. Firmware SA Query capability remains distinct. |
| Experimental MSI BE200 | `iwx` development module | Imported raw/software-crypto path remains opted out | Firmware-dependent combined MIC/FCS tail handling does not prove intact PMF authentication bytes. Private pre-net80211 action processing and association mbuf replacement also require migration behind common authentication and tag preservation. Existing WPA2 HT/VHT/BA work remains separate. |
| Other inherited drivers | Individually enumerated when adopted | No automatic native opt-in | Raw management preservation, software crypto, key epochs and queue retirement must be demonstrated before advertising the new capability. |

The inherited USB configuration includes `athn`, `atu`, `bwfm`, `otus`, `rum`,
`run`, `upgt`, `ural`, `urtw`, `urtwn` and `zyd`. No adopted USB Wi-Fi model
was found in this pinned catalog and named product inventory. USB `ure` /
RTL8153 receipts concern Ethernet. A configured driver is not board acceptance.

## Software verification and physical acceptance

The shared production-source contracts cover selected WPA3-only/transition
profiles, mandatory PMF, malformed IEs/actions, external SAE failure/retry,
tagged key/port operations, same-key reinstall, rekey, same-BSSID reconnect,
copyin races, stop and reconfiguration. CCMP vectors cover the separate
management nonce/AAD/replay domain and PN exhaustion. BIP vectors cover
peer/key identity, final MMIE, authenticated IPN commit and malformed frames.
Driver routing fixtures check rejection before encryption and transport.

Portable host checks are not full kernel or supplicant target builds. Target
build results and CI runs must be recorded against the published revision;
an earlier green parser run does not validate later WPA3 code. Use the selected
SDK LLVM compiler for local compilation, preserving `-Werror`. The existing
GCC compatibility CI lane remains an additional check.

Hardware acceptance must exercise WPA3-only and WPA2/WPA3 transition APs,
required PMF, wrong password, SAE retry, data in both directions, forged and
replayed robust management, group and pairwise rekey, reconnect, stop/restart
and WPA2 regression on each actual board/firmware combination. No hardware
tests are performed by this software task. Mosaico's independently authorized
hardware work remains with its existing owner.

The separate AIC Ethernet-decapsulated RX A-MPDU reorder migration is still
[pending](net80211-rxba.md#firmware-offload-follow-up). It requires an offload
session/sequence/epoch and packet-chain API;
feeding those packets to raw `ieee80211_rxba_input()` is not valid.
