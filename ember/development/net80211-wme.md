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
Slot changes use the incoming capability field before EDCA apply.

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

## Synchronous STA apply

An optional wme_sta_apply callback accepts an immutable effective four-AC set
and the new slot time. The shared pure preparation fills complete effective
records and the compatibility wme_params view, including ACM and noack policy.
The strict STA path commits peer/effective/compatibility tables, count and
QoS flags only after callback success. Failure preserves the previous software
tables and retires the association. Unchanged count skips apply only for the
same applied channel and slot. A slot-only change uses the last valid set when
no valid new WME IE is present.

The callback must be bounded, non-sleeping and synchronous. It must fence new
hardware TX before returning an uncertain hardware error. Software rollback
cannot undo hardware writes. The RTL8822CE consumer calls the actual core EDCA
API under the PCI lock. Core errors quarantine TX and station configuration
until full successful restart. Dying wrappers, pending state changes and
mismatched core channels reject apply before I/O. Registration remains dormant
because the wrapper does not advertise WME.

Strict STA default initialization prepares software only. It does not introduce
a failable callback after sta_join transfers node ownership. The driver owns
hardware defaults before producers. Legacy callbacks retain their existing
void/queued submission contracts; their success is not hardware completion.
AP beacon_update retains its resize boolean. Manual WME parameter ioctl updates
return EOPNOTSUPP before mutation for a strict STA. They need a separate int
transaction before that control operation can be supported.

## Required gates before wrapper WME enablement

Full RTL8822CE MAC initialization restores fixed legacy EDCA register values.
That fact does not prove appropriate AIFS for each current band and slot.
Before enabling WME, the state worker must apply contextual legacy defaults
after channel/slot setup and before parent state or any producer. Apply errors
must stop that transition through the existing quarantine/recovery path.
Current station setup changes SLOT but does not recalculate EDCA AIFS.

The native wrapper also needs four-AC classification and backpressure,
including explicit non-QoS/EAPOL routing. The changed WME and node structures
are an internal kernel ABI change: rebuild the kernel and affected modules
together; do not mix old modules with the new layout. Capability advertisement,
HT/VHT, aggregation and BA remain disabled.

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
It also executes pure preparation, the strict apply transaction, slot updates,
default initialization, sta_join and copy_bss. Reference ownership and kernel
services are modeled; the whole generic state machine is not executed.
Kernel services and unrelated PHY/scan services are modeled. Responses use
actual emitted WME bytes; no test-only setter authorizes the offer. Coverage
includes synchronous producer dispatch, malformed and optional request IEs,
attempt/peer/channel changes, count wrap, all ACI permutations, high TXOP,
transactional malformed sets, admission masks and interleaved sequence domains.
The consequential rt2661 test extracts its real packet-burst decision block;
it does not execute the driver's whole DMA submission function. The PHY native
contract separately executes node_cleanup. The RTW WME contract executes the
actual wrapper callback with the actual core and a fake bus. The startup
contract checks fixed MAC defaults and full quarantine recovery. The complete generic state machine
and all driver compilation/lifecycles require the full target kernel gate.

TEST_RUNNER applies to the executable and propagates failure. CI runs optimized
Memcheck, unoptimized GCC analyzer plus Cppcheck/CSA, and strict ASan/UBSan.
Host checks do not establish driver or physical-device acceptance.
