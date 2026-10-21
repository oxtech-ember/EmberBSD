# Bounded native A-MSDU receive prerequisite

This source prerequisite does not enable HT, VHT, WME or block acknowledgement.
No negotiation path sets `IEEE80211_NODE_AMSDU_RX`.
Unsolicited A-MSDU frames are rejected by default.
The driver continues to advertise its existing legacy profile.

## Owner and profile

The OS owns the parser and its native `net80211` consumer.
The starting source is `2a185206935003c0db58a28a50f785df39c0e910`.
The profile is STA, three-address FromDS QoS data, and a unicast receiver.
Each subframe destination must equal the local station address.
Each subframe source must be a nonzero individual address other than this station.
This conservative local policy rejects reflected traffic in the initial STA profile.
Sources can differ across an AP's bridged subframes.
The aggregate limit is 3839 bytes after removal of the wireless header.
Each MSDU is 8 through 1508 bytes, including its SNAP header.
This deliberately limits Ethernet payloads to the ordinary 1500-byte MTU.
The 22-byte minimum subframe bounds storage and processing without a separate count allocation.

Only RFC1042 or bridge-tunnel SNAP Ethernet II is accepted.
Other LLC forms and aggregate EAPOL are outside this initial profile.
The EAPOL restriction is a local conservative policy, not a claimed universal IEEE requirement.
Legacy individual EAPOL frames retain the existing authorization exception.
Group receivers, address4, QoS-null, HT-control, and fragmented aggregates are rejected.
Encrypted aggregates require CCMP; legacy ciphers are unchanged for individual MSDUs.
An unencrypted aggregate still passes the normal port and privacy policy per MSDU.

## Security and ownership

The parser rejects a destination of `aa:aa:03:00:00:00`.
This is the RFC1042 header confusion mitigation described by the FragAttacks author.
It also validates every length, SNAP prefix, OUI, EtherType, address and intermediate pad extent.
Padding bytes are unspecified and need not be zero.
Trailing padding without another complete subframe is rejected.

Native processing retains decrypt and MIC verification before the parser.
The existing raw BPF tap remains after decrypt/MIC and before decapsulation.
The parser reads mbuf chains through the existing `m_copydata` service.
A complete validation pass precedes the first output allocation.
A second pass creates a bounded list of independently owned Ethernet mbufs.
Allocation failure frees the entire unpublished list.
Malformed final subframes cannot deliver earlier subframes.
Each successful output preserves the input receive interface through native `m_copy_rcvif`.
Ethernet payload alignment uses `ETHER_ALIGN`.

The shared `ieee80211_accept_data` helper retains the existing authorization,
privacy, interface statistics, node statistics, and native delivery path.
The existing `ieee80211_deliver_data` remains the delivery owner.
The aggregate input is freed once after successful list creation.
The caller retains cleanup ownership on every failure.
Future negotiation must set and clear the new node flag with the peer association lifetime.
It must also supply BA/reorder and crypto lifetime requirements before HT advertisement.

## Primary sources and origin

- [OpenBSD input.c](https://github.com/openbsd/src/blob/ca87a5691ce1e78583f1df3ec557f19502fb98d4/sys/net80211/ieee80211_input.c),
  revision 1.263, 2026-05-28: STA destination checks, RFC1042 confusion rejection,
  queued subframes and native delivery ordering.
  The parser retains its three-clause BSD author notice.
  The reader API, full prevalidation, bounds and strict SNAP/EAPOL policy are original adaptations.
- [FreeBSD ht.c](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211_ht.c):
  native subframe splitting and padding reference, under its two-clause BSD notice.
  Its immediate delivery sequence is not copied because this profile rejects malformed aggregates atomically.
- [FragAttacks paper](https://papers.mathyvanhoef.com/usenix2021.pdf), Mathy Vanhoef,
  section 7.2: mitigation for unauthenticated A-MSDU-bit header confusion.
  The paper is a security reference; no implementation is copied from it.

Implementation and contracts are AI-assisted Codex work.
The contracts extract complete current native function bodies and retain their existing source notice.
They do not substitute a second implementation of the native consumer.

## Reproduce and interpret checks

Use host GCC 16.2.0. On macOS the runners select the active SDK for host headers.
The only header shim supplies host string and integer declarations for kernel `sys/systm.h`.
The consumer uses the actual wireless frame definitions from `ieee80211.h`.
Its mbuf allocation, pullup, crypto and delivery boundaries are modeled host services.
The fragmentation double has no cache. It consumes noninitial fragments and returns `NULL`.
The consumer contract checks that this outcome preserves cleanup ownership and stops delivery.
GCC receives the `newbuf`/`m_freem` [allocator/deallocator relationship](https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html) as a semantic attribute.
The plain `malloc` aliasing attribute is not used because the initialized mbuf contains a data pointer.
Clang analyzes the same bodies without GCC's parameterized allocation attribute.
No analyzer warning category or production ownership check is disabled.
The actual `m_copy_rcvif` body runs against modeled `M_PKTHDR` and interface-index fields.
The delivery sink checks index preservation on every output.
The legacy decap function has a narrowly scoped sign-compare diagnostic suppression in its extraction.
No target source diagnostic is suppressed.
Cppcheck suppressions name two layout-only model fields and the unchanged legacy LLC pointer.

```sh
CC=gcc-16 sh ember/tools/net80211-amsdu-parser-contract.sh
CC=gcc-16 sh ember/tools/net80211-amsdu-contract.sh
CC=gcc-16 AMSDU_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  sh ember/tools/net80211-amsdu-parser-contract.sh
CC=gcc-16 AMSDU_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  sh ember/tools/net80211-amsdu-contract.sh
CC=gcc-16 sh ember/tools/net80211-amsdu-analysis.sh
```

The baseline native consumer compiled and failed the valid two-subframe delivery assertion.
The new parser passes boundary/policy fixtures and 50000 bounded malformed inputs.
The native consumer passes 47 behavior checks, including allocation rollback and legacy EAPOL.
GCC16 ASan/UBSan runtime checks pass on the host.
The analysis runner covers the parser and extracted consumer with Cppcheck,
GCC `-fanalyzer` and Clang Static Analyzer using the same host service model.
The inspected tool versions are GCC 16.2.0, Cppcheck 2.22.0 and Apple Clang 21.0.0.
These checks do not validate native kernel mbuf services or real cryptographic authentication.
They do check that no subframe read/delivery precedes modeled successful MIC verification.
Valgrind is unavailable for this macOS arm64 execution platform.
No Valgrind, target kernel build, VM, radio or timing acceptance is claimed.
A coherent GCC16 target build and native legacy regression remain integration gates.
HT negotiation, reorder, interoperability and physical acceptance remain separate work.

## Capture analyzer diagnostics

The manual-only `net80211-diagnostic.yml` workflow uses the same pinned Linux
GCC16 image as the normal contracts. It captures four analyzer configurations:
default, no state merge, no state purge, and neither merge nor purge.
Each configuration enables `-Wanalyzer-too-complex` to expose analysis limits.
The workflow always ends with a diagnostic-only failure, after artifact upload.
It cannot supply an acceptance PASS or replace the normal contract workflow.

Use the collector locally with a new output directory:

```sh
CC=gcc-16 python3 ember/tools/net80211-amsdu-diagnostic.py /tmp/amsdu-evidence
```

The collector reuses the actual consumer runner through a temporary compiler
wrapper. Before runner cleanup, it saves `native.inc`, generated headers,
compiler arguments, GCC version output, preprocessed translation units,
preprocessor exit statuses, analyzer logs and runner exit statuses.
`sha256.json` binds the retained files. Existing output directories are refused.
The collector exits zero when collection completes, including compiler failures.
Read `results.json` and each log to interpret the failures. Mac results do not
establish the Linux analyzer result. State merge and purge variants are diagnostic
experiments, not proposed acceptance flags.
