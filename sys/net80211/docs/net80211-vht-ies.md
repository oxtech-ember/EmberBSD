# Bounded VHT wire values

`ieee80211_vht_ies.c` owns the shared VHT TLV decoder extracted from
`aicwf_assoc_ies_parse()`. The AICWF adapter maps the result to its existing
association metadata. Firmware envelopes, capability qualification and
`ME_CONFIG` construction remain in the AICWF driver.

The pure codec allocates no memory and retains no input pointers. It has no
station, driver, clock or radio dependency. It does not enable VHT, select
bandwidth, establish a rate, or grant channel/DFS transmission permission.
The existing 73-byte raw PHY snapshot and its lifecycle are separate.

## Values and decoding

`struct ieee80211_vht_cap` retains the capability info word, RX MCS map,
RX highest field, TX MCS map and TX highest field. Highest fields preserve
all 16 wire bits, including reserved and extended-NSS encodings.
`struct ieee80211_vht_operation` retains width, both center segments and
the basic MCS map. `struct ieee80211_vht_ies` has separate presence flags.
Absent values are zero; a zero map alone does not establish presence.

`ieee80211_vht_ies_parse(out, data, len)` reads a complete bounded TLV window.
It returns zero on success and minus one on failure. VHT Capabilities
(element 191) must have 12 payload bytes. VHT Operation (element 192) must
have five. Duplicate VHT elements and malformed framing fail. Other complete
TLVs are skipped. Empty input succeeds with absent values. NULL data is
valid only with zero length. A NULL output fails. Failure clears the entire
output so a valid prefix or previous result cannot become fresh evidence.
Input and output for decoding must not overlap.

Decoding is structural. It preserves unsupported or reserved semantic
values for observation. Capability width-set zero excludes 160 MHz and
80+80 MHz; it still includes the base VHT widths, including 80 MHz.
Operation width zero refers to HT 20/40 MHz. Neither value means that this
codec selected a 20 MHz link. Interpreting these values belongs to callers.

The wire layouts were checked against FreeBSD `ieee80211.h` and
`ieee80211_vht.c` at
[`d2145e9b96448da34d3cfede24c509af6d830c92`](https://github.com/freebsd/freebsd-src/tree/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211).
The bounded decoder originates in EmberBSD's BSD-licensed AICWF helper.
No FreeBSD station policy or capability template is imported.

## Explicit payload encoding

`ieee80211_vht_cap_encode(buf, len, cap)` writes exactly 12 payload bytes.
`ieee80211_vht_operation_encode(buf, len, operation)` writes exactly five.
They do not write TLV headers or synthesize capability defaults. Callers
supply every field. Both return zero on success and minus one for NULL
pointers or insufficient output space. Failure changes no output. Extra
space is untouched. Each encoder snapshots its input before writing, so
input and output may overlap. Wire loads and stores support unaligned bytes.

## Software contracts

Run the real source and its existing firmware consumers with GCC 16:

```sh
CC=gcc-16 sh ember/tools/net80211-vht-ies-contract.sh
CC=gcc-16 sh ember/tests/aicwf-vht.sh
CC=gcc-16 sh ember/tests/aicwf-sta.sh
CC=gcc-16 sh ember/tools/net80211-vht-ies-analysis.sh
```

The contracts cover all payload length values, truncation boundaries,
duplicates, unrelated TLVs, NULL inputs, failure clearing, raw bit retention,
byte-exact roundtrips, overlapping encoder inputs and output guards.
The AICWF checks execute the production builder and decoder adapter.
The station contract links the shared source into the actual extracted
management path. `TEST_RUNNER` accepts an executable wrapper, including a
Valgrind Memcheck wrapper. `VHT_IES_TEST_CFLAGS`, `AICWF_VHT_CFLAGS` and
`AICWF_STA_CFLAGS` select compiler diagnostics or sanitizers.
These userland contracts do not qualify a kernel, firmware or physical radio.
