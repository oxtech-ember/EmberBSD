# Intel BE200 Wi-Fi: iwx(4) port proposal (work in progress)

Status: **does not build yet** (782 compiler errors at the first full
build). Nothing here is part of the kernel build: the files live outside
`sys/` until the module compiles, attaches and passes the checks below.

## Target

| Item | Value |
| --- | --- |
| Adapter | Intel Wi-Fi 7 BE200, PCI `8086:272b`, revision `0x1a` |
| Host | MSI Vector 17 HX AI (MS-17S3), Intel Core Ultra 9 275HX |
| OS | EmberBSD 1.0 amd64 release `c5d396f5698b` (NetBSD 11.0 base) |
| Today | `Intel Wi-Fi 7 ... not configured`; only iwi/iwn/iwm exist (up to the 9000 series) |
| Goal, stage 1 | station mode, 11a/b/g legacy rates, WPA2-PSK with net80211 software crypto, DHCP |
| Out of scope for stage 1 | 11n/11ac/11ax/11be, A-MPDU/BA, hardware crypto, MFP/WPA3, power save, monitor mode |

## Chosen approach

Port OpenBSD iwx(4) rather than Linux iwlwifi:

- iwx is a native BSD net80211 driver of the same lineage as iwm(4),
  which NetBSD already ported from OpenBSD (`sys/dev/pci/if_iwm.c`).
  The NetBSD iwm port is the reference for every glue change.
- iwx already supports the BZ device family (`IWX_DEVICE_FAMILY_BZ`),
  the gen3 context info, PNVM loading, and has `IWX_CFG_MAC_TYPE_GL`
  and `IWX_CFG_RF_TYPE_FM` constants. OpenBSD ships BZ with
  `iwx-bz-b0-gf-a0-100` (firmware API 100).
- BE200 is MAC type GL with an FM radio, BZ family. Linux firmware
  `iwlwifi-gl-c0-fm-c0-100.ucode` and `iwlwifi-gl-c0-fm-c0.pnvm`
  exist in linux-firmware with the same API generation.
- EmberBSD net80211 is the NetBSD (pre-VAP) stack with no HT/VHT,
  so the FreeBSD iwx port does not apply.

Upstream sources (OpenBSD `src`, fetched 2026-10-19):

| File | OpenBSD commit | SHA256 of the fetched file |
| --- | --- | --- |
| `sys/dev/pci/if_iwx.c` | `65596250e064` (2026-09-19) | `ade02a314dd9aa63ccc7c87df76099c22f76ec172d42e05a7ad72e0d39450d57` |
| `sys/dev/pci/if_iwxreg.h` | `30e95afee06c` (2026-05-28) | `63c2418dd2cccedf8364375c9a5ca28f6555809065af5ca8f3555ec293091fe8` |
| `sys/dev/pci/if_iwxvar.h` | `e1a9c1b93889` (2026-03-20) | `0e6fd5014b45f0ef7ae2e5e486b9d8d4f8ddb1a5fc2f02865765b156724c7f04` |

Licence: ISC (OpenBSD iwx). The compat header is new and ISC as well.

## What is in `src/`

| File | State |
| --- | --- |
| `if_iwx_compat.h` | Shims for OpenBSD kernel APIs: task(9) on workqueue(9), timeout(9) on callout(9), rwlock names, refcnt, `tsleep_nsec`, `loadfirmware` on firmload(9) (`/libdata/firmware/if_iwx/`), `nitems`, `letoh*`, `mallocarray`, `iwx_free` |
| `if_iwx.c` | OpenBSD file plus mechanical edits: NetBSD includes, `IC2IFP`, `free()` to `iwx_free()`, `DEVNAME()` to `device_xname()`, `NBPFILTER 0` |
| `if_iwxvar.h` | softc: `device_t sc_dev`, `struct ethercom sc_ec`, `krwlock_t`, `pci_intr_handle_t *sc_pihp` |
| `if_iwxreg.h` | unchanged |
| `modules/if_iwx/` | kernel module Makefile and ioconf (`iwx* at pci?`) |
| `files.iwx` | `device iwx: ifnet, arp, wlan, firmload` entry to append to `files.pci` |

## Remaining work, in order

1. **Compat gaps found by the first build** (add to `if_iwx_compat.h`):
   `fls`/`flsl` (use `fls32`/`fls64` from `<sys/bitops.h>`), `M_CANFAIL` (0),
   `caddr_t`, `BUS_DMA_64BIT` (0), `BUS_DMA_ZERO` (absent on NetBSD:
   `memset` the mapping after `bus_dmamem_map`), minimal `struct mbuf_list`
   and `struct ieee80211_rxinfo`, `IEEE80211_BA_MAX_WINSZ` (64),
   `IEEE80211_NUM_TID` (16), a dummy `struct ieee80211_node_switch_bss_arg`.
2. **Compile out stage-2 features** with `#ifdef IWX_HT` (never defined):
   HT/VHT rate setup, RX reorder and BA sessions (`iwx_init_reorder_buffer`
   through `iwx_ampdu_tx_start`), `iwx_rx_reorder`, VHT channel width,
   `iwx_rs_ht_rates`/`iwx_rs_vht_rates`, bgscan, MFP. Keep firmware
   commands valid for 20 MHz legacy operation (PHY context with
   `IWX_PHY_VHT_CHANNEL_MODE20`, rate scaling with legacy rates only).
3. **Rewrite the glue after NetBSD iwm** (`sys/dev/pci/if_iwm.c`, line
   numbers for c5d396f):
   - attach: `iwm_attach` 7758, `iwm_config_complete` 8082 (ethercom,
     `if_initialize`/`ieee80211_ifattach`/`if_percpuq_create`/`if_register`,
     `ieee80211_media_init_with_lock`, `config_mountroot`);
     interrupts with `pci_intr_alloc` + `pci_intr_establish_xname` (start
     with MSI or INTx and `iwx_intr`; MSI-X later);
   - add `PCI_PRODUCT_INTEL_WIFI7` (`0x272b`, already in `pcidevs`) to the match table and a
     case: `sc_fwname = "iwx-gl-c0-fm-c0-100"`,
     `sc_pnvm_name = "iwx-gl-c0-fm-c0.pnvm"`,
     `sc_device_family = IWX_DEVICE_FAMILY_BZ`, `sc_integrated = 0`,
     `sc_uhb_supported = 1`, LTR/xtal values as in Linux `iwl_gl_trans_cfg`;
   - `iwx_newstate`/`iwx_newstate_task`: NetBSD `iwm_newstate` 6354 and
     `iwm_newstate_cb` 6325, `iwm_do_newstate` 6183;
   - `iwx_init`/`iwx_start`/`iwx_stop`/`iwx_watchdog`/`iwx_ioctl`: iwm
     6769-6990 (`ic_mgtq` with `M_GETCTX`, `ieee80211_classify`,
     `ieee80211_encap`, `ieee80211_find_txnode`);
   - RX: `iwx_rx_frame` to the shape of `iwm_rx_rx_mpdu` 3900
     (`ieee80211_find_rxnode`, `ieee80211_input(ic, m, ni, rssi, tstamp)`,
     `ieee80211_free_node`), no reorder;
   - TX: `iwx_tx` encryption via `ieee80211_crypto_encap` as iwm 4592;
     no hardware keys (`ic_set_key`/`ic_delete_key` not installed);
   - scan end: `ieee80211_end_scan(ic)`; begin: `ieee80211_begin_scan(ic, 0)`;
   - EDCA: `ic->ic_wme.wme_chanParams.cap_wmeParams[ac]` instead of
     `ic_edca_ac[]`;
   - node: `ieee80211_free_node` for `ieee80211_release_node`.
4. **Firmware**: install `iwlwifi-gl-c0-fm-c0-100.ucode` as
   `/libdata/firmware/if_iwx/iwx-gl-c0-fm-c0-100` and the PNVM as
   `/libdata/firmware/if_iwx/iwx-gl-c0-fm-c0.pnvm`; record linux-firmware
   commit and SHA256. If API 100 is rejected, try 98/96/92 and record it.
5. **Hardware stages** (each one stops on failure, saves `dmesg`):
   module builds; `modload` attaches and prints hw rev, firmware version,
   PNVM version and MAC address; `ifconfig iwx0 up` and
   `ifconfig iwx0 scan` list access points; `wpa_supplicant` reaches
   COMPLETED with WPA2-PSK; `dhcpcd` lease; ping the gateway;
   `modunload` without panic; reboot with the module and repeat.
6. **Tests**: a host-side contract for the compat shims and for the
   device-config selection of `0x272b` (pattern: `ember/tools/*-contract.c`).

## Building on the target

`build-native.sh` copies `src/` into `/usr/src` and builds the module with
pkgsrc clang. The script prints the error count; `/tmp/iwx-build.log`
holds the full log. The first full build of this snapshot:
`errors: 782` (`-ferror-limit=0`).

## Acceptance (ember-wiki debt N44)

BE200 attaches in GENERIC or as a module with firmware from the release
sets, WPA2 and DHCP to a customer network, survives reboot; physical
check on the MSI; no regressions in kernels without the adapter.
