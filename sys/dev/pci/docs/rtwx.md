# RTL8852BE Wi-Fi driver (rtwx)

The `rtwx` driver supports the Realtek RTL8852BE 802.11ax PCIe adapter
(PCI ID `10ec:b852`, also `10ec:b85b`) on amd64. It attaches the adapter to
net80211 as a station interface. WPA2-Personal uses the software crypto
of net80211 and the base wpa_supplicant.

The driver reuses the Linux rtw89 hardware code instead of a new
implementation. The Linux code is dual licensed, GPL-2.0 OR BSD-3-Clause.
EmberBSD uses it under the BSD-3-Clause terms; see
[the licence file](../rtw89/LICENSE).

## Source layout

| Path | Content |
| --- | --- |
| `sys/dev/pci/rtw89/rtw89_*.c`, `rtw8852b*.c`, `*.h` | Linux v6.6 rtw89 core, PCI, MAC, PHY, firmware, coexistence and RTL8852B files |
| `sys/dev/pci/rtw89/rtw89_compat.[ch]`, `compat/` | Subset of the Linux kernel API: bit fields, locks, work queues, completions, socket buffers, DMA, firmware, NAPI |
| `sys/dev/pci/rtw89/rtw89_bridge.c` | mac80211 replacement for one station interface and one peer |
| `sys/dev/pci/rtw89/if_rtw89.c`, `rtw89_nb.h` | autoconf, net80211, interrupts, transmit and receive glue |
| `sys/dev/pci/rtw89/files.rtw89`, `sys/modules/if_rtw89/` | Kernel configuration and the `if_rtw89` module |
| `external/realtek/rtw89/` | Firmware installed in `/libdata/firmware/if_rtw89` |

The import commit `1d7aadb` contains the Linux files without changes.
Compare a Linux-derived file with that commit to see the adaptations.
Upstream source: tag `v6.6` of `torvalds/linux`,
`drivers/net/wireless/realtek/rtw89`. The `.c` files were renamed with an
`rtw89_` prefix because kernel object names must be unique.
`rtw89_mac80211.c` keeps the Linux mac80211 operations;
`rtw89_bridge.c` calls them.

The adaptations in the Linux files are:

- PCIe ASPM, CLKREQ and L1 substates stay disabled (`rtw89_pci.c`).
- Power save stays disabled (`rtw89_core.c`).
- The PCI probe uses the NetBSD device instead of the Linux `struct device`.
- ACPI DSM queries return `ENODEV` (`rtw89_acpi.c`).
- `rtw89_8852be.c` exports a probe entry for the NetBSD glue.

The firmware is `rtw89/rtw8852b_fw.bin`, version 0.27.32.1, from
linux-firmware. Its SHA256 is
`5b9c224ae014e0aeade8701412d820e213c8880e08615b3905dba89659f21baf`.
`LICENCE.rtlwifi_firmware.txt` permits redistribution.
The newer format-1 file `rtw8852b_fw-1.bin` is not installed: it enables
firmware scan offload and channel contexts, which the bridge does not
emulate.

## Configuration

amd64 `GENERIC` includes `rtwx* at pci?`. The interface name is `rtwx0`.
config(1) does not accept a device name that ends in a digit, so the
device is `rtwx` and the module is `if_rtw89`.

The driver reads the firmware after the root file system is mounted.
A missing firmware file stops the attachment with an error message.

Use the base tools to connect. Keep the configuration file mode 0600:

```sh
ifconfig rtwx0 up
wpa_supplicant -B -D bsd -i rtwx0 -c /etc/wpa_supplicant.conf
dhcpcd rtwx0
```

To load the module on a kernel without `rtwx`:

```sh
modload if_rtw89
```

Debug controls:

| Control | Effect |
| --- | --- |
| `sysctl -w hw.rtw89.debug=1` | Print net80211 state changes |
| `sysctl -w hw.rtw89.debug=2` | Also print each channel change |
| `sysctl -w hw.rtw89.debug=3` | Also print each received frame header |
| `sysctl -w hw.rtw89.hwdebug=<mask>` | Set the rtw89 `RTW89_DBG_*` message mask |
| `modload -i debug=<n> if_rtw89` | Set `hw.rtw89.debug` before attachment |
| `sysctl hw.rtw89.txagg` | Show the number of frames given to the MAC for A-MPDU aggregation |
| `sysctl hw.rtw89.link` | Show the last firmware TX rate (mode, MCS, NSS, width, GI), the RX rate code, the RSSI and the net80211 HT/VHT and TX block-ack counters |

## HT and VHT

The shared net80211 owners negotiate HT and VHT. The driver supplies
the local capabilities and programs the hardware. It does not parse
HT, VHT or block-ack elements itself.

| Function | Owner |
| --- | --- |
| HT20, VHT and HE offer, AP response check, beacon withdrawal | net80211 [`ieee80211_ht20.c`](../../../net80211/ieee80211_ht20.c), [`ieee80211_vht_ies.c`](../../../net80211/ieee80211_vht_ies.c), [`ieee80211_he_ies.c`](../../../net80211/ieee80211_he_ies.c) |
| RX block-ack window, reorder and hole timeout | net80211 [`ieee80211_rxba.c`](../../../net80211/ieee80211_rxba.c); `rtwx` gives the timer (`ic_rxba_ops`) |
| TX block-ack (originator) sessions | net80211 `ieee80211_txba_request()` and `ieee80211_txba_window()` |
| WME parameters of the AP | net80211; `rtwx` applies them with the rtw89 `conf_tx` operation from a work queue |
| Rate selection, short GI, LDPC | rtw89 firmware rate adaptation, from the `ieee80211_sta` fields that the bridge fills |
| Block-ack responses on the air | MAC; the chip creates dynamic BA CAM entries |
| A-MPDU of transmitted frames | MAC, for unicast QoS data of a TID with an established session |

Local capabilities:

- HE (802.11ax) at 20 MHz, or at the 40/80 MHz VHT channel, with
  HE-MCS 0-11 on each stream, LDPC reception and a nominal packet padding
  of 16 us. No MAC capability: no TWT, +HTC reception or multi-user UL;
  rtw89 sends OM Control with UL MU disabled after association. The bridge
  gives the firmware the AP's width, LDPC and PPE Thresholds or padding
  bits, the BSS color and the HE Operation parameters; STBC, DCM and
  beamforming stay disabled. See the
  [net80211 HE contract](../../../net80211/docs/net80211-he-ies.md).
- HT20 with MCS 0-15 on a 2T2R chip (MCS 0-7 on 1T1R), long GI and BCC
  reception, maximum RX A-MPDU 65535 bytes, no MPDU start spacing.
- VHT at the 40 or 80 MHz channel that the AP announces, MCS 0-9 on each
  stream, RX LDPC, short GI reception at 80 MHz and the maximum A-MPDU
  length exponent 7. No 160 MHz, STBC or beamforming.
- At association, the bridge tunes the channel to the negotiated width and
  gives the firmware the HT and VHT words of the AP. The firmware then
  transmits with short GI and LDPC when the AP receives them.
- The kernel log reports the negotiated link, for example
  `rtwx0: VHT80 link on channel 64 (center 58), TX map 0xfffa, SGI 1, LDPC 1`.
- The HT20 profile sets `IEEE80211_HT20_PROFILE_HW_PROT`: each data frame
  has RTS and hardware RTS selection enabled in its TX descriptor, and the
  MAC has hardware CTS-to-self enabled. The station keeps HT when a 2.4 GHz
  AP uses HT protection mode 3. See
  [net80211 hardware protection providers](../../../net80211/docs/net80211-vht-ies.md#hardware-protection-providers).

The receive path accepts MPDUs up to 4096 bytes, so an A-MSDU of 3839
bytes or a VHT MPDU of 3895 bytes fits one buffer. The transmit path keeps
data in the interface send queue while the best-effort DMA ring is full.

## Validation

| Evidence | Result |
| --- | --- |
| Build | `if_rtw89.kmod` and amd64 `GENERIC` with `rtwx` build with the GCC 12.5 cross tools from revision `c5d396f` with the driver commits. The module also builds on `d2eb0bd`. |
| Physical device, 2026-10-16 | Laptop with Intel Celeron N5095A (Jasper Lake), RTL8852BE, rfe type 5, firmware 0.27.32.1. Module loaded into the EmberBSD amd64 `GENERIC` kernel of the same release. |
| Static analysis | Cppcheck 2.22.0, GCC 12.5 `-fanalyzer` and Clang Static Analyzer 21; see below. |

The physical device passed these checks with the module:

- Attachment, firmware download, efuse read and the MAC address.
- Scan on 2.4 GHz and 5 GHz, including the DFS channel 52 and channel 12.
- WPA2-PSK (CCMP) association with an 802.11ax WPA2/WPA3 transition-mode
  access point on 5 GHz channel 52, and DHCP.
- ICMP through the interface without loss, and a 30 MB HTTPS download at
  about 13 Mbit/s without receive or transmit errors.
- Interface down, module unload, module load and a new association.

### HT and VHT, 2026-11-26

The same laptop model booted a `GENERIC`-based kernel with built-in
`rtwx` and the HT/VHT change. The AP is 802.11ax/be, WPA3-SAE with CCMP,
BSS on 5 GHz channel 64 at 160 MHz. The station negotiated VHT at
80 MHz (primary 64, center 58) with two streams, short GI and LDPC.

TCP, 128 MiB per run, between the laptop and a second Wi-Fi client of the
same AP. The second client uses 802.11be at 160 MHz and 2161-2401 Mbit/s,
so the figures are the limit of the laptop link. The receive time is the
sender time to the closed connection.

| Kernel | Laptop to peer | Peer to laptop |
| --- | --- | --- |
| Legacy 11a, before the change | 24.5 Mbit/s | about 20 Mbit/s |
| VHT80, RX A-MPDU at most 8191 bytes | 162-179 Mbit/s | 60-75 Mbit/s |
| VHT80, RX A-MPDU 64 KiB (HT) and exponent 7 (VHT) | 192-214 Mbit/s | 116-127 Mbit/s |
| Same, 18 min, 44 runs in each direction | 129 Mbit/s mean (93-167) | 95 Mbit/s mean (83-105) |

During the 18-minute run, 11.8 GB moved without a disconnection, ping
lost 3 of 1076 replies (mean 7 ms) and the interface counted no errors.
The firmware reported three L1 SER events (`0x1000`-`0x1002`); the chip
recovered each one without a link loss. The same events occur with the
legacy profile. Receive used about 8 % of the CPU time in interrupts.

`hw.rtw89.link` showed the firmware transmitting at VHT MCS 9, two
streams, 80 MHz with short GI (866 Mbit/s on the air), RX at VHT MCS 7 on
two streams and an RSSI of -33 to -42 dBm. Four parallel TCP connections
moved 308 Mbit/s from the laptop and 187 Mbit/s to it, with about 90 %
idle CPU time. So a single TCP connection, not the radio link, limits the
figures in the table above.

A second peer, a Wi-Fi client with 802.11ax at 160 MHz and 2161-2401
Mbit/s, gave higher figures with the same kernel:

| TCP | Laptop to peer | Peer to laptop |
| --- | --- | --- |
| One connection, default socket buffers | 230-256 Mbit/s | 164-167 Mbit/s |
| One connection, 4 MiB buffers | 314-318 Mbit/s | 193-206 Mbit/s |
| Four connections, default buffers | 383 Mbit/s | 266 Mbit/s |

The default `kern.sbmax`, `net.inet.tcp.sendbuf_max` and
`net.inet.tcp.recvbuf_max` are 256 KiB. With a round-trip time of about
10 ms under load, that window limits one connection to about 200 Mbit/s.
While the laptop received 167 Mbit/s, the CPU that serves the interrupt
was about 30 % busy and the other CPUs were idle.

### HE, 2026-11-27

A kernel with the HE change associated as `HE80 link on channel 52
(center 58)`. The firmware transmitted at HE-MCS 11 on two streams at
80 MHz (1201 Mbit/s on the air) and received at HE-MCS 5-7. With the
802.11ax peer at 160 MHz:

| TCP, default socket buffers | Laptop to peer | Peer to laptop |
| --- | --- | --- |
| One connection | 218-258 Mbit/s | 170-176 Mbit/s |
| Four connections | 451 Mbit/s | 268 Mbit/s |
| Two connections, 12 min, 32 runs each way | 365 Mbit/s mean (313-378) | 222 Mbit/s mean (202-230) |

The 12-minute run moved about 10.6 million frames from the laptop without
a disconnection or SER event; ping lost none of 20 replies. On 2.4 GHz
channel 1 the station ran HE20 at HE-MCS 9-10 on two streams, and one
connection moved 94.9 Mbit/s from the laptop.

### 2.4 GHz, 2026-11-26

The same AP on 2.4 GHz channel 1 uses HT protection mode 3. Before
`IEEE80211_HT20_PROFILE_HW_PROT`, net80211 withdrew the HT profile at the
first beacon and the station returned to INIT before the 4-way handshake.
With the flag the station runs HT20 with MCS 0-15; the firmware transmits
at MCS 13-15. One TCP connection moved 74-76 Mbit/s from the laptop and
43 Mbit/s to it.

This AP refuses several association attempts on 2.4 GHz before it answers
the 4-way handshake, and 60 seconds after each association it sends a
protected deauthentication with reason 4 (inactivity), also during TCP
traffic. The station then joins the 5 GHz BSS. This is AP policy for a
dual-band station; a long 2.4 GHz run needs an AP without it.

Roaming and Bluetooth coexistence under load are not tested.

### Static analysis

The analysis used the module compile options and target headers.
Cppcheck used the predefined macros of the target GCC.
The Linux files were also analysed at the import commit with the same
API emulation, to separate inherited findings from port findings.

- Cppcheck reports 29 findings in the Linux files: void pointer
  arithmetic, redundant return checks, dead branches and two `%d`
  formats. The import commit reports the same 29 findings.
- Clang reports 85 findings in the Linux files, mostly dead stores in the
  coexistence code. The import commit reports the same findings.
- GCC `-fanalyzer` reports three inherited NULL dereference paths in
  hardware scan, remain-on-channel and beacon-filter report code. The
  bridge does not call these paths with firmware 0.27.32.1.
- Clang found a NULL dereference in `rtw89_queue_work()` when the system
  work queue does not exist. This is fixed.
- Cppcheck and Clang reported a NULL `dev_get_drvdata()` result in the
  unused Linux suspend and resume handlers. The handlers are not built on
  NetBSD.
- Two Cppcheck `%zu`/`%zx` reports in the glue are false positives: the
  arguments are `size_t`. Inline suppressions name the reason.
- Cppcheck reports one cast in the NetBSD header `sys/pslist.h`. It is
  outside the driver.

No executable host contract exists for the driver yet, so no Valgrind
or sanitizer run applies. The physical checks above are the only runtime
evidence.

## Link recovery regression

Run the extracted-source regression with the selected native Python and GCC16:

```sh
"$SDK_PYTHON" ember/tools/rtw89-link-loss-contract.py \
    --source-root "$PWD" --work-dir "$TEST_WORK" --cc "$SDK_CC"
```

`TEST_WORK` must name an existing temporary directory. The runner executes the
actual watchdog, beacon validator and state worker against bounded kernel and
hardware shims. It covers idle supervision, malformed and foreign beacons,
timer wraparound, CSA cleanup ordering, automatic rescan and interface stop.
It does not replace kernel or physical-device testing.

## Limits

- Station mode only. No access point, monitor or IBSS mode.
- HT20, VHT 40/80 MHz and HE 20/40/80 MHz with A-MPDU in both
  directions; see [HT and VHT](#ht-and-vht). HE 160 MHz, multi-user and
  TWT, and HT40 without VHT are not negotiated.
- Software crypto only. WPA3-Personal uses the opt-in common net80211
  SAE/PMF owner; its new software integration has not yet completed physical
  acceptance. See the [consumer matrix](../../../net80211/docs/wpa3-consumers.md).
- No power save. ASPM, CLKREQ and L1 substates are disabled.
- Link supervision uses a software watchdog independent of firmware beacon
  reports. Only a complete, structurally valid beacon from the associated AP
  on the selected channel refreshes the deadline; expiry calls
  `ieee80211_beacon_miss()` so net80211 probes and then scans.
- Outside RUN, `rtwx` keeps `if_timer` armed while net80211 waits for an
  AUTH or ASSOC reply. Without a reply, `ieee80211_watchdog()` returns the
  station to SCAN. `ember/tests/rtwx-watchdog.py` checks this path.
- On a matching channel-switch or Quiet announcement, `rtwx` pauses the MAC
  TX scheduler synchronously. Its state worker completes the required INIT,
  resets the rtw89 PCI rings, and starts a fresh scan. An explicit pending
  reconnect state is retained; interface shutdown cancels automatic recovery.
- These software paths do not establish physical RF or long-run acceptance.
- amd64 only. The DMA emulation assumes cache-coherent DMA below 4 GB.
- RTL8852BE only. Other rtw89 chips are not built.
