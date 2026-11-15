# Native SAE hardware TX: approved direction and proposed contract

Status: design requirements, reviewed against EmberBSD
`f49cf06683492c867e36c6d1e29ff763a4efc609` on 2026-11-15.
Firmware encryption and TX packet-number (PN) allocation are an approved
direction. The shared TX API and its adapter implementation do not exist yet.
The first intended consumer is the BZ/MLD path in `iwx`; other native providers
retain software TX until they implement the same common contract.

## What exists

- [ieee80211_sae.h](../ieee80211_sae.h) defines an **RX-only** provider:
  `fence`, full-set `replace`, and authenticated RX metadata. It has no TX PN
  import/export or unchanged-key identity contract.
- In [ieee80211_sae.c](../ieee80211_sae.c), `sae_set_key()` avoids resetting
  an identical software key. However, changing a GTK or IGTK calls
  `sae_hw_replace()`, which sends the complete live set, including the PTK,
  under a new publication serial. That serial is not proof of new key material.
- In [iwx](../../../ember/development/iwx-be200/src/if_iwx.c),
  `iwx_sae_replace()` drains traffic, removes installed keys and adds the
  supplied set. `iwx_sae_key_cmd()` zeroes the command, leaves `tx_seq` zero,
  and sets `IWX_SEC_KEY_FLAG_NO_TX`. Common software CCMP owns TX PN today;
  common replay checks remain authoritative on RX.
- `iwx_mld_sta_cmd()` already sets `sta_cmd.mfp = 1` for native SAE.
  `IEEE80211_SAE_HW_NO_QOS_TX` keeps SAE data on the non-QoS path, avoiding
  firmware-originated TX BlockAck negotiation whose PMF protection is unproven.

Removing `NO_TX` alone would let a GTK/IGTK update reinstall an unchanged PTK
with a zero firmware TX counter. It would also leave software TX encryption
and queue policy unresolved. The present RX-only path is not evidence of that
TX nonce-reuse defect occurring today.

## Ownership and capabilities

Extend the existing SAE owner and provider contract; do not add a parallel
security owner in `iwx`. The following are semantic requirements, not new C
symbols or an implemented ABI.

| State or operation | Required owner |
| --- | --- |
| SAE/EAPOL, selected security profile, controlled-port authorization | Existing supplicant and common net80211 owner |
| Key identity, lifetime, unchanged/replace/remove decision, transaction publication | Common net80211 owner |
| PTK TX encryption and PN allocation in the first offload profile | Firmware, for both data and robust unicast management, including host-submitted and firmware-generated actions |
| RX authentication, replay and group robust management | Existing authenticated RX provider and common replay/BIP owner; TX offload does not transfer these implicitly |
| Firmware slots, command encoding, DMA, queue drain and acknowledgements | Adapter |

Select TX ownership when installing a fresh PTK and keep it for that key's
lifetime. A single authority must allocate PN for every supported nonce domain
and every transmitter using that key. Separate CCMP traffic/replay domains and
BIP IPN are not interchangeable counters. Do not create independent host and
firmware allocators for the same domain. The first profile leaves group robust
TX unsupported, as in the existing station owner.

Capabilities must distinguish authenticated RX, PTK data TX, robust-management
TX, protected autonomous BlockAck actions, and retention of unchanged keys.
QoS/TX aggregation is enabled only when all its security prerequisites hold.
The adapter must reject an unsupported combination before making a key usable.
An adapter without these capabilities continues to use the existing software
TX profile. This is not permission to fall back mid-key after a firmware error.

## Key transaction and failure rules

1. **Identify the change in the common owner.** Give a key a stable opaque
   lifetime identity independent of the set's publication serial and slot.
   Identical-key requests preserve TX PN, RX replay and authorization state.
   GTK/IGTK rotation must not delete/re-add an unchanged PTK. The adapter can
   retain key identities/handles without maintaining another persistent copy
   of the complete key set.
2. **Fence and drain.** Block new host traffic and autonomous firmware traffic
   affected by the transaction. Drain or discard old queued packets, retries
   and aggregation work before replacing their key or releasing buffers.
   Every asynchronous command and completion carries its captured association,
   key and operation identity. A stale completion cannot authorize a new set.
3. **Apply only the delta.** Keep unchanged firmware keys and their counters;
   install genuinely new keys, remove retired keys, and check each acknowledgement.
   Borrowed key buffers remain valid only for the documented call; asynchronous
   command storage has explicit ownership and is erased when no longer needed.
   Define bounded completion, timeout and cancellation before adding callbacks.
4. **Publish once.** Only a complete current transaction can release the fence.
   Successful rekey may preserve prior authorization; installing a key never
   grants it. Partial failure, timeout or uncertain hardware state keeps the
   controlled port closed and PMF required, then retires the connection.
5. **Reset safely.** The first implementation reconnects and derives a fresh
   PTK after reset or loss of counter state. It must not replay the old key set
   at PN zero. An in-place recovery would need a separate, proven contract for
   retaining or restoring a counter beyond every PN already allocated, including
   queued and autonomous transmissions. A software shadow is not that proof.
6. **Do not switch allocator under a live key.** Prefer a fresh-key handshake
   when changing TX ownership. Stop before PN exhaustion; never wrap. Explicitly
   distinguish plaintext for firmware encryption from completed software
   ciphertext, so neither bypass nor double encryption is possible.

## Protected BlockAck is a separate prerequisite

PMF covers robust action frames, including ADDBA/DELBA. An enabled station MFP
bit or a successful key command does not prove that firmware-generated actions
use the current PTK and correct PN. Before removing the non-QoS restriction,
establish the firmware command/queue contract and verify the resulting frames
on air. Keep host-originated actions on the same selected protection path.
Receiving an unprotected or replayed robust action must not start a BA session.

## Required checks for the implementation

Extend the [native SAE tests](sae.md#opt-in-native-owner) and the existing
[iwx fixtures](../../../ember/development/iwx-be200/README.md#software-verification-and-bench-remainder):

- Repeated PTK install and GTK/IGTK rotation leave the live PTK handle and TX
  counter intact. Changed PTK, deletion and slot reuse cannot inherit stale work.
- Rekey under queued data, management and autonomous BA traffic preserves nonce
  uniqueness and RX replay checks. The software-TX providers keep their behavior.
- Failed/partial install, wrong or late acknowledgement, cancellation, timeout,
  RFkill, reset and PN exhaustion keep admission closed without disabling PMF.
- A reset with unknown PN requires a fresh PTK; reusing key bytes with a new
  host serial does not satisfy this condition. Verify frame-format selection
  and absence of an automatic same-key software fallback.

Then build the affected kernel/modules. On the matching firmware, correlate
key commands, queue events and radio captures: protected ADDBA/DELBA, other
robust management, unchanged-key PN continuity, replay rejection and rekey/reset.
Measure sustained throughput in both directions only after those checks.
Hardware applicability and physical results belong in
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/docs/reader-guide.md).
Neither throughput nor a host fixture alone establishes this contract.

## Upstream evidence

[Linux iwlmld key programming](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/key.c)
initializes `tx_seq` from the key's TX PN and handles individual keys.
Its [mac80211 integration](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/mac80211.c)
selects hardware TX aggregation; its
[station setup](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/sta.c)
sets MFP. These are implementation references, not proof of EmberBSD's on-air behavior.
[mac80211 crypto documentation](https://docs.kernel.org/driver-api/80211/mac80211-advanced.html#hardware-crypto-acceleration)
also separates IV generation, management encryption and key activation.
[FreeBSD net80211 crypto](https://github.com/freebsd/freebsd-src/blob/main/sys/net80211/ieee80211_crypto.c)
keeps cipher policy and serialized device-key callbacks in the common subsystem.
Reuse that separation; preserve the fork's stronger unchanged-key and retirement
guarantees rather than copying a donor's lifecycle wholesale.
