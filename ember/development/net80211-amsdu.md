# Bounded native A-MSDU parser prerequisite

This parser does not enable HT, VHT, WME or block acknowledgement.
Native integration and negotiation remain separate gates.
The starting source is `2a185206935003c0db58a28a50f785df39c0e910`.

## Parser policy

The intended profile is STA, three-address FromDS QoS data, and a unicast receiver.
Every subframe destination must equal the local station address.
Every source must be an individual, nonzero address other than this station.
Sources can differ across an AP's bridged subframes.
The aggregate limit is 3839 bytes after wireless header removal.
Every MSDU is 8 through 1508 bytes, including the SNAP header.
This limits Ethernet payloads to the ordinary 1500-byte MTU.
The minimum subframe length bounds iteration and storage without a count allocation.

The parser validates length, inter-subframe padding extent, addresses, LLC and SNAP.
It accepts RFC1042 and bridge-tunnel SNAP Ethernet II only.
It rejects the RFC1042 confusion destination `aa:aa:03:00:00:00`.
Padding bytes need not be zero.
Trailing padding without another complete subframe is rejected.
Aggregate EAPOL and reflected source addresses are conservative local exclusions.
These exclusions are not claimed as universal IEEE requirements.
Individual legacy EAPOL processing is outside this parser.

The reader callback allows native mbuf-chain access without flattening the aggregate.
Only a fully validated aggregate can become eligible for allocation and delivery.
The future native consumer must first validate all subframes, then allocate every output.
It must retain decrypt/MIC, port authorization, privacy, counters and delivery ownership.
The parser alone cannot establish those native network-path properties.

## Primary sources and provenance

- [OpenBSD input.c](https://github.com/openbsd/src/blob/ca87a5691ce1e78583f1df3ec557f19502fb98d4/sys/net80211/ieee80211_input.c),
  revision 1.263, 2026-05-28: RFC1042 confusion and STA destination validation policies.
  The parser retains the original three-clause BSD author notice.
  Its reader API, bounds and strict SNAP/EAPOL policy are original adaptations.
- [FreeBSD ht.c](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211_ht.c):
  native splitting and padding reference, under its two-clause BSD notice.
  Its immediate delivery sequence is not copied.
- [FragAttacks paper](https://papers.mathyvanhoef.com/usenix2021.pdf), Mathy Vanhoef,
  section 7.2: mitigation for unauthenticated A-MSDU-bit header confusion.
  This is a security reference; no implementation is copied from it.

Implementation and contracts are AI-assisted Codex work.
A source-only reviewer other than the implementer checked the parser and its proposed native consumer.

## Host contract

```sh
CC=gcc-16 sh ember/tools/net80211-amsdu-parser-contract.sh
CC=gcc-16 AMSDU_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  sh ember/tools/net80211-amsdu-parser-contract.sh
```

GCC 16.2.0 executes the actual parser with literal boundary and policy fixtures.
The contract also runs 50000 bounded malformed inputs.
The self-source rejection regression failed before the policy check and passed after it.
ASan/UBSan pass on the macOS arm64 host.
Valgrind is unavailable on this execution platform.
Native integration, target builds, real cryptography, VM and physical acceptance remain unclaimed.
