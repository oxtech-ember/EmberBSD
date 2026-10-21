# Native STA WME negotiation prerequisite

This change tightens shared net80211 negotiation and sequence bookkeeping.
The RTL8822CE wrapper retains its legacy producer and capability settings.
It does not enable WME, HT/VHT, aggregation or block acknowledgement.

A STA may authorize QoS only after its native ASSOC_REQ or REASSOC_REQ actually
contains one valid WME information IE. The management output path records
successful queue admission before producer dispatch. It binds the current
node, BSSID, channel, request subtype and local attempt generation. Allocation
or prepend failure, a new attempt, leave and node cleanup retire the offer.
Scan capability information alone cannot authorize QoS. Queue admission does
not prove DMA completion, transmission or ACK. Association response frames
have no attempt nonce: a delayed response from the same AP/channel cannot be
cryptographically distinguished by the local generation.

The matching association response consumes the pending offer. Unrelated peer,
channel or subtype responses do not consume it. Missing or unsolicited WME
retains legacy operation. Malformed or duplicate offered parameter IEs abort
the association through the existing scan transition. Subsequent WME beacon
updates require the current associated peer, channel and validated negotiation.
Other beacon processing retains its existing policy.

Parameter parsing validates exact extent, vendor/type/subtype/version,
reserved bits, all four unique ACIs, AIFSN and contention window ordering
before committing a temporary candidate. TXOP remains a canonical u16 count
of 32 microsecond units. Count zero applies the first valid set; unchanged
counts are ignored only after complete validation and a previous commit.

The shared classifier uses the STA's authoritative self parameter table for
ACM. Priority ordering is BK < BE < VI < VO. Admission downgrade follows
VO → VI → BE → BK and rejects a frame when all eligible classes require
admission. Ethernet and IPv4 classification reads bounded bytes through mbufs.
Non-QoS data, EAPOL and management use TX sequence slot zero. QoS TID n uses
slot n+1, matching existing receive domains. Counters are not reset for an
association retry. rt2661 packet-burst bookkeeping follows the actual frame's
QoS/non-QoS domain. Other slot-zero action/management producers, ioctl and
statistics retain their layout.

## Required gates before wrapper WME enablement

The shared update chain still ignores int driver callback errors. Default
initialization and sta_join ownership need an explicit failure contract;
sta_join cannot simply return false after replacing ic_bss. AP beacon_update
returns a resize boolean, which must not be repurposed as errno.

The compatibility wme_params table is not filled by the current parser/update
chain, although rt2860, run and iwm consume it. The canonical validated table,
compatibility view and actual hardware apply state must be reconciled in the
next error-propagation atom. The new valid flag denotes a validated peer set,
not successful hardware programming. Deferred driver enqueue is not hardware
completion. None of those remaining gaps is accepted as working WME here.

The native wrapper still needs serialized EDCA apply/error handling, four-AC
classification and backpressure, including explicit non-QoS/EAPOL routing.
The changed node fields are an internal kernel ABI change: rebuild the kernel
and affected modules together; do not mix old modules with the new layout.

## Host source contracts

```sh
CC=gcc-16 sh ember/tools/net80211-wme-contract.sh
CC=gcc-16 WME_TEST_CFLAGS='-O0 -g -fanalyzer' WME_ANALYZE=1 \
    sh ember/tools/net80211-wme-contract.sh
CC=gcc-16 WME_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/net80211-wme-contract.sh
```

The runner executes complete native management construction/output,
association response, beacon, parser, classifier, encap and sta_leave functions.
Kernel services and unrelated PHY/scan services are modeled. Responses use
actual emitted WME bytes; no test-only setter authorizes the offer. Coverage
includes synchronous producer dispatch, malformed and optional request IEs,
attempt/peer/channel changes, count wrap, all ACI permutations, high TXOP,
transactional malformed sets, admission masks and interleaved sequence domains.
The consequential rt2661 test extracts its real packet-burst decision block;
it does not execute the driver's whole DMA submission function. The PHY native
contract separately executes node_cleanup. The complete generic state machine
and all driver compilation/lifecycles require the full target kernel gate.

TEST_RUNNER applies to the executable and propagates failure. CI runs optimized
Memcheck, unoptimized GCC analyzer plus Cppcheck/CSA, and strict ASan/UBSan.
Host checks do not establish driver or physical-device acceptance.
