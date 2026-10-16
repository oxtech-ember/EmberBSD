# RTL8852BE Wi-Fi driver (rtwx)

The `rtwx` driver supports the Realtek RTL8852BE 802.11ax PCIe adapter
(PCI ID `10ec:b852`, also `10ec:b85b`) on amd64. It attaches the adapter to
net80211 as a station interface. WPA2-Personal uses the software crypto
of net80211 and the base wpa_supplicant.

The driver reuses the Linux rtw89 hardware code instead of a new
implementation. The Linux code is dual licensed, GPL-2.0 OR BSD-3-Clause.
EmberBSD uses it under the BSD-3-Clause terms; see
[the licence file](../../sys/dev/pci/rtw89/LICENSE).

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

Boot of the `GENERIC` kernel with built-in `rtwx` is not tested on the
device. Long-run operation, roaming, 2.4 GHz association and Bluetooth
coexistence under load are not tested.

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

## Limits

- Station mode only. No access point, monitor or IBSS mode.
- Legacy 802.11a/b/g rates only. HT, VHT, HE and aggregation are disabled
  because the NetBSD net80211 does not negotiate them.
- Software crypto only. WPA3/SAE and protected management frames are not
  supported.
- No power save. ASPM, CLKREQ and L1 substates are disabled.
- No beacon-loss detection. net80211 inactivity timers end a lost
  association.
- amd64 only. The DMA emulation assumes cache-coherent DMA below 4 GB.
- RTL8852BE only. Other rtw89 chips are not built.
