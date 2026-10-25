# Bounded STA BlockAck foundation

The shared implementation is [ieee80211_rxba.c](../ieee80211_rxba.c).
Its provider contract is [ieee80211_rxba.h](../ieee80211_rxba.h).
This is a software receive foundation with a bounded originator. It does not
configure hardware BlockAck responses itself; aggregation and BlockAck
handling on the air belong to the driver and its MAC.

## Ownership and bounds

The receiver supports TIDs 0 through 7, windows up to 64 MPDUs and 12-bit
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

Run the actual-source consumer with GCC16:

```sh
CC=/absolute/gcc16/bin/gcc sh ember/tools/net80211-rxba-contract.sh
```

Set `RXBA_ANALYZE=1` for GCC path analysis, Cppcheck and Clang Static Analyzer.
Clang is used only for analysis. `RXBA_TEST_CFLAGS` selects runtime sanitizer
flags. `TEST_RUNNER` can name a supported Memcheck wrapper.

The shared and native fixtures execute Action/output, key lifecycle, CCMP/AES,
input, timer and Ethernet continuations. Kernel allocation, locks, scheduling,
clocks and physical input are modeled services. Causal cases cover stale
standalone A-MSDU ownership, key purge, response publication, allocation limits,
BAR ingress and deadline servicing. These tests do not qualify radio aggregation,
hardware BlockAck, scheduler latency or endurance.

The [RTL8822CE boundary](../../dev/ic/docs/rtl8822ce-proposal.md#current-source-boundary) and
[board record](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/README.md) track target checks separately.
