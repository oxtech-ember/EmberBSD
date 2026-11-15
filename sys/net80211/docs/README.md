# Wi-Fi driver integration map

Read the [architecture and standards overview](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/wifi.md)
before selecting an integration path. This page indexes the implemented interfaces; declarations and focused contracts remain authoritative.
Keep hardware profiles, firmware applicability, and physical acceptance in
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/docs/reader-guide.md).

## Select the provider model

| Device interface | Starting consumer | Responsibility split |
| --- | --- | --- |
| Raw 802.11 management and data frames | [rtw88_net.c](../../dev/ic/rtw88_net.c) | Native net80211 constructs and processes frames; the adapter supplies radio state and frame transport |
| Firmware-managed association and Ethernet data | [bwfm.c](../../dev/ic/bwfm.c), [if_aicwf.c](../../dev/sdmmc/if_aicwf.c) | Firmware owns the MAC operations declared by its ABI; the adapter translates common requests and events |
| Native protocol with authenticated hardware crypto RX | [SAE hardware provider](../ieee80211_sae.h) | Shared host key/replay/PMF owner plus transactional RX key installation and trusted per-frame metadata |

These are source examples, not templates to copy whole.
Bus attachment is independent of MAC ownership. Keep firmware envelopes, register access, DMA, IRQ, and chip sequences in the adapter.
Keep shared frame validation, security policy, and reorder algorithms in net80211 or the supplicant.

## Base interface

Start with [ieee80211com](../ieee80211_var.h), [protocol declarations](../ieee80211_proto.h),
and the fork's [ieee80211(9)](../../../share/man/man9/ieee80211.9).
The retained NetBSD interfaces are the base; the optional EmberBSD providers below extend them.

| Integration point | Interface to use | Required behavior |
| --- | --- | --- |
| Network device lifecycle | `if_init`, `if_stop`, `if_start`, `if_ioctl`; `ieee80211_ifattach`, `ieee80211_ifdetach`, `ieee80211_media_init` | Fill interface identity, rates, channels, and capabilities before attach. Install optional providers after attach initializes their defaults. Stop hardware and drain deferred work before releasing its state. |
| State changes | `ic_newstate` and [ieee80211_proto.c](../ieee80211_proto.c) | Native wrappers preserve the common state handler and apply hardware state in the required order. Firmware adapters implement their declared management overrides. Do not infer completion from command submission. |
| Raw frame path | `ieee80211_input`, `ieee80211_encap`, node references, `ic_mgtq` | Preserve management frames and RX status needed by the selected common consumer. Follow mbuf and node ownership on success, error, and deferred handoff. See [input](../../../share/man/man9/ieee80211_input.9) and [output](../../../share/man/man9/ieee80211_output.9). |
| User control | `ieee80211_ioctl`, [ieee80211_ioctl.h](../ieee80211_ioctl.h) | Reuse shared validation and authorization. Keep unsupported operations explicit. The [BSD supplicant backend](../../../external/bsd/wpa/dist/src/drivers/driver_bsd.c) is the existing userland consumer. |
| Key installation | `ic_crypto.cs_key_alloc`, `cs_key_set`, `cs_key_delete`, `cs_key_update_begin`, `cs_key_update_end` in [ieee80211_crypto.h](../ieee80211_crypto.h) | Retain common software defaults where applicable. Hardware callbacks must preserve key lifetime and fence asynchronous work around key changes. |
| Station authorization | `ic_sta_set_port` when the adapter needs a hardware/firmware controlled port | Propagate apply failure. Association and RUN alone do not authorize protected data. See [port implementation](../ieee80211_ioctl.c) and [port tests](../tests/t_sta_port.py). |

Use the current channel and capability fields as declarations of implemented operations.
An IE received during scanning is peer input, not local capability or negotiated state.
Register only providers whose prerequisites the adapter can meet.

## Optional provider contracts

| Feature | Integration interface | Contract |
| --- | --- | --- |
| Native SAE and software PMF | `ieee80211_sae_attach`, `ieee80211_sae_state_request`, `ieee80211_sae_retire`, `ieee80211_sae_tx_current` | [SAE](sae.md#opt-in-native-owner): raw management transport, software CCMP, synchronous queue fences, and supported atomic serial publication |
| Native authenticated hardware RX | `ieee80211_sae_attach_hw`, `ieee80211_sae_hw_ops`, RX tagging in [ieee80211_sae.h](../ieee80211_sae.h) | Key replacement by instance and RX drain, hardware-key generation on received frames; shared replay checks remain active. TX stays in software, or firmware owns pairwise TX with the `IEEE80211_SAE_HW_TX_*` capabilities |
| FullMAC external SAE | `ieee80211_ioctl_sae` with probed capabilities and a chip callback | [SAE ioctl boundary](sae.md#external-sae-ioctl-boundary): shared authorization/copyin/clearing; adapter state checks and firmware key/association operations |
| Station actions and connected scans | `ic_sta_mgmt`; `ieee80211_sta_scan_begin`, `ieee80211_sta_scan_report`, `ieee80211_sta_scan_release` | [Station management](station-management.md): selected security, action directions, peer/generation/cookie checks, and physical drain before scan replacement |
| Strict STA QoS | `ic_wme.wme_sta_apply` | [WME](net80211-wme.md#synchronous-sta-apply), [TXOP](net80211-txop.md): validate all queues, apply synchronously, commit shared state only after success |
| HT and VHT admission | `ic_ht20_admit`, `ic_vht_admit` | [HT provider header](../ieee80211_ht20.h), [VHT contract](net80211-vht-ies.md): current local readiness and channel profile; no capability from scan observations alone |
| Raw BlockAck and aggregation | `ic_rxba_ops`, `ieee80211_txba_request`, `ieee80211_txba_window` | [RX/TX BA](net80211-rxba.md), [A-MSDU](net80211-amsdu.md): bounded scheduling, session lifetime, reordering before crypto replay, and explicit hardware aggregation |
| Firmware-decapsulated reorder | `ieee80211_rxba_offload_*` | [Offload receiver](net80211-rxba.md#firmware-offload-receiver): complete packet chains, explicit session/key/association generations, and final current-generation delivery check |
| Probe and transmit withdrawal | `ic_scan_probe_admit`, `ic_sta_quiet`, `ic_sta_csa` | [Probe policy](net80211-scan-probes.md), [TX quiet](../../dev/ic/docs/rtw88-tx-quiet.md): bound probe admission and close TX synchronously before deferred recovery |

No optional row is enabled merely by registering the base network interface.
For HT/VHT, follow the current provider and source contracts; the basic native HT profile and experimental wider-channel profile differ.
An RX-only crypto provider is not a general TX encryption-offload API.
The [TX-offload requirements](sae-hw-tx.md) specify the common key/PN lifetime
and protected-BlockAck prerequisites. [Firmware TX providers](sae.md#firmware-tx-providers)
describes the implemented API; on-air acceptance is not done.

## Association, work, and buffer lifetime

Read [association-lifecycle.md](../association-lifecycle.md) before implementing asynchronous state work.
The common association epoch, the SAE serial, and firmware operation cookies have different owners.
Never substitute the current epoch for the identity captured when old work was queued.

- Request native SAE retirement before queuing a leaving state transition or taking the driver's own lock.
- Close admission at stop/reset. Drain work that can access hardware before reporting its resources reusable.
- Keep common node/key operations in the serialization required by their contract. A separate driver lock does not replace it.
- Halt physical timers and workers before detach frees their owner. Logical cancellation alone is insufficient.
- Honor each API's mbuf ownership. Raw input and firmware-offload packet-chain APIs have different contracts.

The native reference uses common construction/validation and a saved parent state handler.
FullMAC references retain firmware command formats and acknowledgements while calling common validators and lifecycle helpers.
When firmware cannot provide the metadata required by a shared operation, leave that capability disabled or extend the common contract explicitly.

## Checks to reuse

Use the check commands documented with the affected contract; do not create a second command list here.

| Changed boundary | Existing checks and consumer |
| --- | --- |
| Station selection or event identity | [Association lifecycle fixtures](../tests/assoc-lifecycle.py), [BSD supplicant backend](../../../external/bsd/wpa/dist/src/drivers/driver_bsd.c) |
| Request validation, native SAE, key retirement | [SAE checks](sae.md), [crypto](net80211-crypto.md), [BIP](net80211-bip.md) |
| Action transport or scan cancellation | [Station and adapter fixtures](station-management.md#software-checks) |
| QoS, HT/VHT, aggregation | [WME checks](net80211-wme.md#host-source-contracts), [VHT checks](net80211-vht-ies.md#software-contracts), [BA checks](net80211-rxba.md#source-checks) |

Review the selected adapter's actual callback and teardown path, not only a pure parser.
Then build the affected kernel and matching modules with the selected SDK.
Host fixtures do not establish target compilation, firmware behavior, or an over-the-air connection.
Physical acceptance must cover the advertised mode, both traffic directions, failure, reconnect, rekey, and stop/restart where applicable.
Record that evidence once in the corresponding DevBoards profile.
