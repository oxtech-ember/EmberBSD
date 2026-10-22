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
mismatched core channels reject apply before I/O. The native wrapper explicitly
sets C_WME and F_WME after ifattach and strict consumer registration. It prepares
software defaults after media initialization creates the BSS node. Shared
ifattach's disabled default policy stays unchanged. These source contracts do
not establish physical radio QoS acceptance.

Strict STA default initialization prepares software only. It does not introduce
a failable callback after sta_join transfers node ownership. The driver owns
hardware defaults before producers. Legacy callbacks retain their existing
void/queued submission contracts; their success is not hardware completion.
AP beacon_update retains its resize boolean. Manual WME parameter ioctl updates
return EOPNOTSUPP before mutation for a strict STA. They need a separate int
transaction before that control operation can be supported.

## Native WME initialization and producer ordering

Full RTL8822CE MAC initialization restores fixed legacy EDCA register values.
That fact does not prove appropriate AIFS for each current band and slot.
The native state worker now applies contextual EDCA after channel/slot setup
and before the parent state callback can submit new frames. Legacy operation
uses the same DCF policy on all four ACs: AIFSN2, CWmax exponent10, TXOP0,
and CWmin exponent5 for 11b or4 for ERP/OFDM. Band and slot determine AIFS.
No uninitialized shared WME table supplies the capability-off defaults.

The wrapper snapshots user-configured media separately from peer-selected
mode. Initial attach uses the selected media entry; the user word is still
IFM_NONE until a media ioctl. AUTO without a fixed rate uses the requested channel flags. A fixed
rate retains its framework-resolved mode binding. Unsupported contexts fail
before channel or station writes. The worker does not acquire the media lock;
a media change can wait for that worker during restart.

A matching negotiated RUN context reapplies the accepted effective AC set.
A missing or mismatched QoS/channel/slot context stops the transition instead
of substituting legacy defaults. EDCA errors leave new producers fenced and
stop the core through its existing quarantine/recovery path. This ordering
does not flush frames already in DMA. Hardware pause after failed I/O remains
uncertain. Full successful restart clears the core quarantine.

The native wrapper classifies Ethernet traffic before encapsulation and selects
BE, BK, VI or VO from the accepted peer context. Non-QoS data and EAPOL use BE;
management retains its own queue. An invalid accepted QoS context requests the
existing INIT fence outside queue and PCI locks.

For a normal FIFO, target-queue capacity is checked before dequeue, sequence
allocation and crypto. A full queue retains the original Ethernet mbuf and
releases its node reference. Completion retries the same queued packet.
ALTQ/TBR can change their selected head between poll and dequeue. Their path
checks every data AC before dequeue, then classifies the actual returned frame.
This conservative gate can block an available AC behind a full unrelated AC.
A token-limited NULL dequeue leaves queue ownership unchanged. Giant serializes
the non-MPSAFE producer and discipline changes. Completion accounts for all five
hardware queues before it clears the watchdog.

These paths support native legacy WME advertisement. Physical radio QoS
acceptance remains separate. The changed WME and node structures are an internal
kernel ABI change: rebuild the kernel and affected modules together; do not mix
old modules with the new layout. HT/VHT, aggregation and BA remain disabled.

## Host source contracts

```sh
CC=gcc-16 RTW88_START_ANALYZE=1 sh ember/tools/rtw88-start-contract.sh
CC=gcc-16 RTW88_ROUTING_ANALYZE=1 sh ember/tools/rtw88-routing-contract.sh
CC=gcc-16 RTW88_ENABLE_ANALYZE=1 sh ember/tools/rtw88-enable-contract.sh
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
contract checks fixed MAC defaults and full quarantine recovery. It also
extracts the complete native state worker, newstate, init and media callback.
The worker calls the actual channel, station and EDCA core operations. Its
modeled parent callback submits an actual core frame inline and checks that
contextual EDCA was written first. The contract covers every EDCA I/O failure
before and after effect, mode/cache transitions, negotiated-context mismatch
and full restart. It executes native ifmedia_init_with_lock, ifmedia_set and
the attach media initialization block; media-list population/lookup is modeled. Scheduling, node services and the parent callback are modeled;
the complete generic state machine
and all driver compilation/lifecycles require the full target kernel gate.

TEST_RUNNER applies to the executable and propagates failure. CI runs optimized
Memcheck, unoptimized GCC analyzer plus Cppcheck/CSA, and strict ASan/UBSan.
Host checks do not establish driver or physical-device acceptance.

The routing runner executes complete native start, newstate and softintr with
actual shared classification and encapsulation, actual policy and the actual
core on a fake bus. It extracts the owning FIFO and IFQ macros and runs both
ALTQ and no-ALTQ builds. Scheduler services, node references, kernel services
and unrelated RX input are modeled. A full-queue case retains all real node
sequence slots and does not invoke the crypto consumer. This proves that the
PN consumer is not reached; it does not execute CCMP/AES in that combined path.
The separate crypto contracts own cipher and PN validation.

The enable runner executes complete native attach and state work, plus actual
shared ifattach, setmode and media initialization. Registration, node allocation,
protocol defaults, media-list services and unrelated driver entry points are
modeled. Node allocation occurs at late attach, as in the owning stack. The
actual management producer places a complete WME offer in a core MGMT buffer.
A matching response carries distinct AP parameters; strict apply and the RUN
worker must write all four exact AC words before the first data producer.
No test setter grants QoS authorization. Negative paths include missing offers,
malformed or mismatched responses, pending/dying state and EDCA failure before
and after effect. Failed apply preserves software tables and prevents RUN.
Legacy AP responses retain the non-QoS BE path. The bitset service asserts its
256-channel input domain; foreign/ANYC IBSS pointers are not qualified by this
model. This precondition does not repair the shared setmode implementation.

The native routing contract executes real WME flag macros and the complete
BPF mbuf-tap consumer. `M_PROMISC` and the low WME AC bit both use `M_LINK1`.
Queue-capacity probes preserve the original flags. The Ethernet tap consumes
its duplicate marker before authoritative classification and encapsulation.
The final class must still match the reserved queue. Data AC metadata is
cleared before the raw tap; the serialized QoS TID remains unchanged.
Management retains its original duplicate marker. Tests cover all four ACs,
injected markers, split Ethernet headers and crypto dispatch. They execute
native `m_move_pkthdr` and `ieee80211_mbuf_adjust`. Allocation, writable storage,
BPF delivery and pullup coalescing remain host services; COW, real MMU layout
and the complete kernel networking path are not qualified by this model.

GCC16's analyzer reports `analyzer-deref-before-check` at O0 and O2 on the
extracted encap function: it equates the original input dereference with the
NULL check after assignment from `ieee80211_mbuf_adjust`, whose return may
legitimately be the original buffer, a replacement or NULL. The fixture scopes
that one diagnostic exemption to that exact post-call check on GCC16. All
other encap lines and the complete adjust body retain analyzer diagnostics;
Clang CSA and sanitizer checks remain enabled. No production source, storage
service behavior or global warning policy is changed.
