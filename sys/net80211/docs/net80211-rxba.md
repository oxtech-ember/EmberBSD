# Bounded STA BlockAck foundation

The shared implementation is [ieee80211_rxba.c](../ieee80211_rxba.c).
Its provider contract is [ieee80211_rxba.h](../ieee80211_rxba.h).
This is a software receive foundation with a bounded originator. It does not
configure hardware BlockAck responses itself; aggregation and BlockAck
handling on the air belong to the driver and its MAC.

## Ownership and bounds

The raw 802.11 receiver supports TIDs 0 through 7, windows up to 64 MPDUs and 12-bit
sequence arithmetic. It rejects ambiguous half-range jumps. The per-node
2 MiB limit includes state, candidates, windows and retained mbuf storage.
External allocations and tags are charged conservatively. Chain and tag walks
have fixed limits. A ciphertext slot records its charge before decryption.

Actual Action input produces an ADDBA response through the shared management
queue. Scheduler preflight and peer checks precede that publication. The
queue commit installs the candidate window without another failing operation.
Rejected publication does not report an agreement. Uncertain scheduling or
ownership withdraws the peer through the existing INIT transition.

Reordering precedes duplicate detection and CCMP replay processing. Actual key
replacement and node cleanup purge retained ciphertext before their mutations.
Slots and callbacks do not retain borrowed key pointers. The input consumer
checks the current peer, HT profile, authorization and security capsule.
A standalone negotiated A-MSDU receives that check even without a BA window.

The native driver registers an interface-owned MPSAFE callout. It passes only
numeric tokens to the existing Giant-held soft interrupt. Shared node and key
work stays there. Cancel does not wait under PCI; detach halts the callback
before freeing its owner. Hole deadlines use the oldest retained arrival plus
100 ms. Negotiated TU inactivity is separate. A bounded receive batch compares
arrival times with pending deadlines and services overdue holes under load.

The narrow BAR ingress accepts a 20-byte compressed single-TID BAR for the
current authorized peer and local address. CRC, ICV, C2H and length gates stay
active. This does not admit arbitrary short control frames or configure an
on-chip BA agreement.

## Originator (TX) sessions

`ieee80211_txba_request()` sends an ADDBA Request (IEEE Std 802.11-2016
§9.6.5.2) for each TID in its mask that has no session, while the peer is
current as above: RUN, the associated AP, authorized, negotiated HT. The
request asks for immediate block ack, a 64-MPDU buffer, no A-MSDU in A-MPDU
and no timeout, with the starting sequence number that encap assigns next
to that TID. It is queued through the shared management queue and so starts
output; callers stay outside the driver start routine.

A matching ADDBA Response (same TID and dialog token, current association)
with status 0, immediate policy and a nonzero buffer establishes the
session with a window of at most 64. A refusal, a response that is not
received within 1000 ms or a DELBA from the recipient idles that TID for
10 s; three requests per TID and association bound the exchange. The
originator sends no BlockAckReq and keeps no inactivity timer; it relies on
the recipient's DELBA and reorder timeout, as the upstream rtw88 driver
does. `ieee80211_txba_window()` reports the established window for one TID
of the associated AP in RUN, and 0 otherwise. Sessions belong to one HT
attempt and are cleared with the peer's receive state. The numeric
`tx_diag` counters (requests, accepts, refusals, timeouts, DELBAs, invalid
responses, last status) survive that reset.

The driver decides per frame whether an MPDU of an established session is
aggregated. Without a session nothing changes on the air.

## Null data and A-MSDU

QoS Null frames pass through ordering before CCMP. Authenticated Null data is
freed after the decrypted BPF tap and never becomes Ethernet payload, including
frames with an added LLC body. Empty protected Null payloads remain rejected
by the existing CCMP bounds; this work does not expand the AES interface.

A validated admitted HT response publishes the bounded A-MSDU receive grant.
HT reset, rejected or legacy negotiation, node cleanup and direct key deletion
clear it. Successful key installation and rekey keep that negotiated grant,
while old BA ciphertext is purged before the cipher context changes.
The existing two-pass A-MSDU parser rejects oversized or malformed aggregates
without partial Ethernet delivery.

## Source checks

Run the actual-source consumer through the SDK native-check route, with the
selected native prefix and compiler family. The normal compiler is Clang/LLVM.
For example, run the SDK's `scripts/run-native-check.py` with these arguments:

```sh
--host-prefix "$HOST_PREFIX" --compiler-family clang --required-role python \
  --tool sh --cwd "$OS_SOURCE" --log "$CHECK_LOG" --bindings "$CHECK_BINDINGS" \
  -- ember/tools/net80211-rxba-contract.sh
```

Set `RXBA_ANALYZE=clang` and select the SDK `cppcheck` role for exhaustive
Cppcheck and Clang Static Analyzer. `RXBA_ANALYZE=1` keeps the explicit GCC
compatibility lane and adds GCC path analysis. Each analyzer must reject a
causal null-dereference fixture. `RXBA_TEST_CFLAGS` selects runtime sanitizer
flags. `TEST_RUNNER` can name a supported Memcheck wrapper.

The shared and native fixtures execute Action/output, key lifecycle, CCMP/AES,
input, timer and Ethernet continuations. Kernel allocation, locks, scheduling,
clocks and physical input are modeled services. Causal cases cover stale
standalone A-MSDU ownership, key purge, response publication, allocation limits,
BAR ingress and deadline servicing. These tests do not qualify radio aggregation,
hardware BlockAck, scheduler latency or endurance.

The [RTL8822CE boundary](../../dev/ic/docs/rtl8822ce-proposal.md#current-source-boundary) and
[board record](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/README.md) track target checks separately.

## Firmware-offload receiver

`ieee80211_rxba_offload_*()` receives opaque packet chains after firmware
processing. It shares sequence insertion, window advancement, contiguous
release, storage measurement and hole expiry with the raw receiver. The two
owners retain separate negotiation and security lifetimes. Raw ciphertext
continues through `ieee80211_input_ordered()`; offload output returns complete
packet chains for the adapter's existing Ethernet delivery path.

The offload owner supports 16 TIDs and windows of 1 through 64 MPDUs. Start
supplies an explicit SSN and session generation. Input supplies a TID, 12-bit
sequence, association generation, pairwise-key generation and session
generation. Input cannot start a session. Stop explicitly drains or discards.
Late, duplicate and exactly half-space sequences are rejected. A new start
must exceed that TID's last session generation, including after retirement or
allocation failure. Association/key high-water marks survive retirement.
Generation exhaustion prevents reuse; only destruction of the owner ends
these marks.

Each input consumes one exclusively owned, acyclic `m_nextpkt` chain, including
all error returns. A slot retains its head, tail and charged allocation size.
The 2 MiB limit includes owner state, windows, mbufs, external storage and tags.
An MPDU contains at most 256 packets; each packet has at most 16 segments and
16 tags. These walks are bounded before retention. Invalid input is returned
separately as `rejected`, without a tail walk under the state lock. Other
output consists of per-TID ready batches and a discard chain. The caller
initializes each output to zero, then delivers or frees it outside the state
lock. It must serialize producers through delivery and make the final
`offload_current()` check atomic with handoff and retirement.

This API accepts complete MPDU chains. It does not yet collect firmware-split
A-MSDU subframes by index/LAST or accept an explicit NSSN window advance.
An Intel-style consumer needs those extensions in the shared layer, including
PN/index/generation checks, bounded incomplete ownership and deferred advance.
Submitting each subframe independently with the same sequence would incorrectly
treat the later subframes as duplicates.

Ready batches carry association, key and session generations. Drain permits
the retiring run's ready batches until restart, discard or an epoch change.
Discard invalidates them immediately. Absolute hole deadlines derive from the
oldest retained arrival. Timer tokens change on rescheduling; stale tokens
cannot age a replacement window. Clock regression, deadline overflow or timer
serial exhaustion retires retained state. Provider detach must halt its
physical callback before releasing the device owner.

### AIC adapter

[if_aicwf.c](../../dev/sdmmc/if_aicwf.c) maps the checked firmware reorder flag
to explicit start and drain operations, with a 64-slot window and 50 ms hole
timeout. The first frame of an observed reorder run supplies the SSN. Firmware
owns negotiation, BlockAck responses, decryption/replay and the controlled
port. The driver retains metadata decoding, firmware commands and Ethernet
conversion. It no longer owns a separate sequence-window algorithm.

The descriptor contract is the Radxa AIC8800 SDIO source at
[`d13d07963cd15d731e2895e8288a04cca6152ac9`](https://github.com/radxa-pkg/aic8800/blob/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_rx.h).
Its VIF and STA indexes are checked along with the transmitter/BSSID and local
receiver address. STA `0xff` is accepted only with those address checks.
The ABI provides no BA-session or key-generation identifier. A host generation
is captured before each FIFO read. A connection or key event cannot relabel
later data from that same read. Host queues and pending output are retired
before deferred stop/reconfigure or pairwise-key work. Key confirmation resumes
RX only if its saved generation is still current and the controlled-port
command succeeded. Duplicate PTK installation preserves firmware PN state.
A canceled host CONNECT request cannot reopen RX through a late indication.

`sc_epoch_lock` protects state and final handoff. `sc_rx_lock` serializes data
and timer producers through handoff; its lock order is RX then epoch. Retirement
takes only epoch and can invalidate output between mutation and handoff.
The returned chains are consumed after window mutation releases the state
lock. The final epoch check and `if_percpuq_enqueue()` then share that lock.
The current driver has no detach entry point; a future detach must halt
`sc_rxba_ch` before releasing the softc.

These fences cannot identify an old packet still held inside firmware before
a later FIFO read. Same-BSSID reconnect, key changes and unreported BA restart
therefore retain a firmware-boundary limitation. A full wire-generation claim
requires a documented firmware identifier or an acknowledged drain operation.
The host no longer assumes firmware has removed every duplicate: late and
repeated sequences are dropped. These changes require aggregate-loss and
reconnect acceptance on the physical board.

### Software and physical checks

The common fixture exercises multi-packet ownership, all 16 TIDs, wrap and
half-space sequence boundaries, window displacement, byte budgets, malformed
storage, allocation failure, absolute deadlines, stale tokens, drain/discard,
rekey, reconnect and generation exhaustion. The AIC fixture executes actual
RX decapsulation, the common core, metadata admission, packet handoff and
key/state callbacks, including retirement between mutation and delivery and
a control event inside one FIFO batch. Existing raw BA/CCMP/native-timer and
AIC station/SAE/PMF/A-MSDU checks remain active.

Host models and target builds do not prove radio behavior. Physical acceptance
still covers aggregation under load, loss and timeout recovery, firmware event
ordering, same-BSSID reconnect, pairwise rekey and stop/reconfigure with the
actual AIC firmware. No physical session is part of the software checks.
