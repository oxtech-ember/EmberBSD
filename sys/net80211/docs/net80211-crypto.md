# Native net80211 crypto contracts

## Key-ID input bounds

The generic receive consumer checks the minimum WEP frame size and the
supplied header space before reading the key-ID byte. QoS, four-address
and padded headers can exceed the legacy 24-byte header. The key-ID read
must fit before the selected cipher checks its complete IV and trailer.
Invalid lengths increment the existing `is_rx_tooshort` counter.

Run the actual-source receive consumer with modeled native mbuf services:

```sh
CC=gcc-16 sh ember/tools/net80211-crypto-bounds-contract.sh
CC=gcc-16 CCMP_ANALYZE=1 CCMP_TEST_CFLAGS='-O1 -g -fanalyzer' \
    sh ember/tools/net80211-crypto-bounds-contract.sh
CC=gcc-16 CCMP_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/net80211-crypto-bounds-contract.sh
```

The contract covers 24, 26, 28, 30, 32 and 36-byte header space, truncated
key IDs, invalid signed lengths and the existing header pullup path.
It executes the complete generic RX consumer. The mbuf and cipher services
are explicit test models. It does not test cryptographic correctness,
radio traffic, HT/VHT operation or a physical device.

## CCMP replay domains

Software CCMP keeps one receive PN counter for each QoS TID, including
TIDs 8 through 15. Non-QoS traffic retains the existing key scalar and
scalar key ioctl representation. QoS counters live in the cipher's private
context. Other cipher key structures and driver key layouts do not change.
The native key-install ioctl supplies the initial RSC before the cipher
seeds its private counters. A new key reseeds every QoS domain.

The receive consumer checks three-address, four-address and padded header
space before reading the TID and IV. It rejects repeated or lower PNs in
the selected domain. A PN in another TID does not raise that domain's
floor. Only a frame that passes decryption and MIC verification advances
the selected counter. Replay events report that counter's previous value.
The 48-bit PN decoder uses unsigned arithmetic for its high byte.

This adapts the per-TID replay semantics from FreeBSD net80211 at
[`d2145e9b96448da34d3cfede24c509af6d830c92`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211_crypto_ccmp.c).
The BSD-licensed reference uses a shared 17-slot key array. This fork keeps
16 QoS slots private and preserves its existing non-QoS scalar ABI.
Existing source licenses and notices remain in place. OpenAI Codex assisted
the adaptation and regression harness.

```sh
CC=gcc-16 sh ember/tools/ccmp-contract.sh
CC=gcc-16 CCMP_ANALYZE=1 CCMP_TEST_CFLAGS='-O1 -g -fanalyzer' \
    sh ember/tools/ccmp-contract.sh
CC=gcc-16 CCMP_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
    sh ember/tools/ccmp-contract.sh
```

The contract executes the complete native CCMP cipher, generic receive
consumer, key-install ioctl and replay-event producer. It also executes
the native AES-CCM and constant-time BearSSL AES implementation, including
the existing RFC 3610 known-answer tests. Kernel allocation, mbuf storage,
key lifecycle services and event delivery are explicit test models.
Coverage includes PTK and GTK2 ioctl RSC seeds, rekey, key isolation,
interleaved TIDs, non-QoS traffic, duplicate PNs, bad MICs, PN limits,
chained payloads, malformed headers and padded QoS headers.

Each fragment still consumes its own increasing PN. This checks cipher
replay handling, not fragment reassembly. This change adds no BA session,
reorder window, WME/HT/VHT capability, PMF policy or hardware-crypto proof.
Same-TID out-of-order input still requires a separate receive reorder path.
The host contracts do not qualify target execution or physical radio traffic.
