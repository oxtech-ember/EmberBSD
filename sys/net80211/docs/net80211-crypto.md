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
TIDs 8 through 15, and a separate counter for robust management frames.
Non-QoS data retains the existing key scalar and scalar key ioctl
representation. QoS and management counters live in the cipher's private
context. Other cipher key structures and driver key layouts do not change.
The native key-install ioctl supplies the initial RSC before the cipher
seeds its private counters. A new key reseeds every private domain.

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

Each data fragment still consumes its own increasing PN. This checks cipher
replay handling, not fragment reassembly. This change adds no BA session,
reorder window, WME/HT/VHT capability, PMF policy or hardware-crypto proof.
Same-TID out-of-order input still requires a separate receive reorder path.
The host contracts do not qualify target execution or physical radio traffic.

## CCMP management protection primitive

The cipher authenticates the complete management subtype in AAD and sets
bit 4 of the CCM nonce flags for management frames. Data AAD masking and
QoS nonce priority remain unchanged. The management RX counter is separate
from all data domains; authentication failures leave the selected counter
unchanged. The common TX counter covers data and management, permits the
last 48-bit PN, then fails before changing either the packet or counter.
This also applies when the cipher supplies an IV for hardware encryption.

Management input and output require protected, unicast PV0 deauthentication,
disassociation or action frames with a 24-byte header. DS address formats,
HT-Control, fragments, empty bodies and reserved CCMP IV bits are rejected.
Action category policy belongs to the authenticated management consumer.
Group management protection uses BIP. Calling `ccmp_setkey` reseeds replay
state, so a same-material key reinstall must be handled by the key owner
without resetting this cipher. The consumer remains responsible for peer,
association and key-epoch checks and for selecting software crypto where
management hardware crypto has not been qualified.

The AAD rule follows the BSD-licensed
[FreeBSD correction a2d087b67e14](https://github.com/freebsd/freebsd-src/commit/a2d087b67e14ea2a2496d6424df8d9668e271177),
which identifies IEEE 802.11-2020 12.5.3.3.3. The nonce management bit is
specified in 12.5.3.3.4 and independently confirmed by
[mac80211's CCM construction](https://github.com/torvalds/linux/blob/v6.18/net/mac80211/wpa.c).
The independent management replay domain is also documented in
[mac80211's key state](https://github.com/torvalds/linux/blob/v6.18/net/mac80211/key.h).
Linux sources were consulted for protocol behavior; no GPL implementation
was copied. The [official mac80211 hardware-crypto documentation](https://docs.kernel.org/driver-api/80211/mac80211-advanced.html#hardware-crypto-acceleration)
separately describes management TX, RX and IV requirements; ordinary data
crypto capability alone is not management crypto qualification.

The actual-source contract includes four independently constructed
management known-answer fixtures: deauthentication, disassociation, a short
SA Query action body, and a 31-byte action-body crypto fixture. Synthetic
action bodies exercise the cipher only, not action-category admission.
The key is `000102030405060708090a0b0c0d0e0f`, nonce is
`10022233445566010203040506`, and AAD is the subtype plus `40`, the
three addresses `021122334455`, `022233445566`, `023344556677`, and
`0000` sequence/fragment bytes. Literal ciphertext and tags are compared
on TX and supplied independently to RX, including split mbufs.
The optional generator uses an external OpenSSL AES primitive and the
[RFC 3610](https://www.rfc-editor.org/rfc/rfc3610) CCM construction,
without using the cipher under test:

```sh
/absolute/sdk/bin/python3.15 ember/tools/ccmp-contract-vectors.py \
    /absolute/sdk/bin/openssl
```

The shared AES-CCM mbuf traversal subtracts each skipped segment's length
before advancing to its successor. This matters when a CCMP header ends
exactly at the first mbuf boundary: subtracting the next segment's length
instead can skip payload bytes or underflow the offset. The contract covers
that boundary in both management TX and RX, and compares the mbuf helper
with the contiguous AES-CCM core across empty and uneven segments. Bad-tag
cases require complete payload zeroing while preserving prefix and suffix
bytes. The cursor repair and these fixtures are original EmberBSD changes
prepared with OpenAI Codex assistance.

Tests also cover subtype tampering, bad MICs, all 17 data replay domains
against the management domain in both directions, management rekey seeds,
invalid key lengths, malformed management headers, reserved IV bits, and
TX PN exhaustion for software and software-IV hardware paths. The existing
data, RFC 3610 AES, generic key selection and ioctl tests remain in the same
suite. Use the selected native Clang through the
[SDK host-check entry point](../../../ember/tools/native-net80211-checks.md);
GCC16 and Memcheck remain separate CI checks. `CCMP_ANALYZE=1` enables
Cppcheck and Clang Static Analyzer, and `CCMP_TEST_CFLAGS` accepts the
ASan/UBSan flags shown above. This primitive does not by itself establish
complete PMF, SAE, WPA3 or hardware support.
