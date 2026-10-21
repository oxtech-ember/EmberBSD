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
