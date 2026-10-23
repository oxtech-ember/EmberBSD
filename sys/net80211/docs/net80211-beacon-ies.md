# Bounded beacon IE views

`ieee80211_beacon_ies_parse()` decodes a complete information-element window.
It accepts no MAC header or fixed beacon body. It does not allocate memory.
The native beacon consumer and the bwfm/aicwf scan adapters share this parser.
Firmware envelopes, channel policy and lifecycle checks stay in the callers.

## Ownership and errors

Every pointer in `struct ieee80211_beacon_ies` borrows the input window.
Keep that window alive until the consumer has copied the required data.
`ieee80211_add_scan()` copies the data that its cache retains.
The PHY snapshot owns its bytes and uses `ieee80211_phy_ies_parse()`.
PHY observations do not negotiate rates, aggregation or channel width.

The parser returns zero on success and minus one on malformed framing.
It rejects incomplete headers, truncated bodies and short FH, DS or TIM IEs.
FH needs five payload bytes, DS one, and TIM four.
An error clears the entire result. No partially decoded pointers escape.
Malformed HT/VHT observations clear only the PHY snapshot, as before.
Complete unknown elements remain acceptable.
Vendor fixed-field reads check both the declared payload and remaining window.
WPA needs four payload bytes. WME classification needs six payload bytes.

CSA (element 37) has three payload bytes. Extended CSA (element 60) has four.
Each element must be unique, use mode zero or one, and name a nonzero channel.
Counts zero through 255 are accepted. The parser preserves both borrowed views
when both forms occur. It does not resolve operating classes or tune a radio.
A malformed or repeated switch element rejects the complete window.
Consumers must handle parser failure before making a transmission decision.
These views alone do not implement DFS client admission or channel switching.

`ieee80211_sta_beacon_announcement()` classifies a complete fixed beacon body
and IE window. `ieee80211_sta_action_announcement()` recognizes the two CSA
Action prefixes. Both return a withdrawal decision without changing state.
They allocate no memory and retain no input pointers. A caller must first
validate the RX envelope and current peer identity. These classifiers never
authorize transmission or refresh a beacon lease by themselves.

Quiet (element 40) has six payload bytes: count, period, duration and offset.
It must be unique. All field values remain observations; the parser does not
schedule a quiet interval. Malformed or repeated Quiet rejects the window.

Most fields select the last matching element. Separate views retain admission:

- `ssid` and `rates` select the last occurrence, even if too long.
  The native consumer retains its existing mandatory-element validation.
- `ssid_valid` and `rates_valid` select the last element within the existing
  32-byte SSID and 15-byte supported-rate limits. The aicwf adapter uses these.
  bwfm continues to use firmware SSID/rate metadata.
- `ds_exact` selects the last one-byte DS element for aicwf.
  `ds` also accepts longer DS elements for native and bwfm compatibility.
- `extcap_bounded` preserves bwfm's one-through-32-byte capability window.
  Native and aicwf retain the complete `extcap` view.
- `security` preserves native/bwfm ordering between RSN and WPA vendor IEs.
  `rsn` preserves aicwf's RSN-only selection. A vendor IE at the exact end
  of the window is valid.
- A repeated WME IE sets `wme_duplicate`. Native WME admission still owns
  parameter validation and association state.

`timoff` starts at the IE window. Native adds the MAC/fixed-body prefix.
aicwf adds its fixed beacon-body size. bwfm retains its firmware-IE offset.
These offsets do not change the public scan ABI.
Channel observations never override caller policy. Native off-channel rejection,
aicwf adjacent-channel reception, and firmware channel conversion remain separate.

## Optional STA quiet provider

`ic_sta_quiet` defaults to NULL at protocol attach. A driver can register it
for a conservative exit policy. It runs under Giant, without a driver lock,
and must close every TX producer synchronously without sleeping. It must keep
transmission closed through the subsequent shared INIT request and cleanup.

Before HT or legacy beacon validation, the native consumer checks current STA
AUTH, ASSOC or RUN identity. The node must be the current BSS on the current
channel. Source and BSSID must match the selected peer. A matching CSA or
Extended CSA or Quiet calls the provider, purges HT/RX BA storage and timer ownership,
then requests INIT. Any countdown, including
zero, exits immediately. A malformed fixed body or IE window also exits.
Both beacons and probe responses use this policy. Foreign peers, other states,
other channels and interfaces without a provider retain their existing path.

This interface does not tune a successor channel or establish a DFS lease.
The Action receiver recognizes Spectrum Management CSA (category 0, action 4)
and Public Extended CSA (category 4, action 4). It copies only the two-byte
prefix across mbuf segments. The same peer/channel/lifecycle checks apply.
The destination must be local or multicast; encrypted, fragmented, ordered and
DS-directed headers do not invoke the provider. A recognized prefix exits even
when the remaining payload is malformed. No successor is decoded or followed.
The existing Block Ack Action validation remains in force for other frames.

No radio provider is registered by this change. Beacon-loss policy,
transmit-power constraints and driver hardware quiescence remain separate
prerequisites before DFS channel admission.

## Checks

Use a shared object directory and a host GCC 16 compiler:

```sh
python3 ember/tools/net80211-beacon-ies-contract.py --work-dir "$OBJDIR"
python3 ember/tools/net80211-beacon-ies-contract.py --work-dir "$OBJDIR" \
    --cflags='-fsanitize=address,undefined -fno-omit-frame-pointer' --analyze
CC=gcc-16 sh ember/tests/aicwf-scan-channel.sh
CC=gcc-16 sh ember/tools/net80211-wme-contract.sh
CC=gcc-16 sh ember/tools/net80211-phy-ies-contract.sh
```

Set `TMPDIR` to the compiler temporary directory. Set `TEST_OBJDIR` to the
shared object directory for the shell contracts.
The Python runner removes its private child directory after success or failure.
Its `--baseline-revision` option executes the current bwfm regression against
that revision's complete scan adapter. It must fail on the old short-TIM path.
The old adapter's redundant byte-channel range comparison remains a reported
compiler warning for this comparison; current sources compile with `-Werror`.

The tests cover IE bounds, filtered duplicate views, exact-end vendor IEs,
three production consumers, native scan storage and scan serialization.
The bwfm fixture models firmware channel decoding and the OS service boundary.
The WME fixture executes the complete native management consumer; its PHY
service remains modeled. Separate parser/storage contracts use the real PHY
parser. These checks do not execute firmware or qualify kernel/hardware behavior.
Use Valgrind on a supported host; ASan/UBSan is a substitute, not a Valgrind run.

## Origin

The Quiet wire layout was checked against `struct ieee80211_quiet_ie` in
[FreeBSD ieee80211.h at d2145e9b96448da34d3cfede24c509af6d830c92](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211.h).
The conservative consumer exits immediately for every Quiet element.
It does not claim scheduled pause/resume support.

The implementation is an original, AI-assisted extraction of the EmberBSD
scan contracts at `b245e1c2a81cccb15ed877f0f2b403dba07d1609`.
The existing drivers and native consumer retain their source notices.
The PHY helper retains its existing wire-layout provenance.

Prior art informed the boundary, not a wholesale stack import:
[OpenBSD bwfm](https://github.com/openbsd/src/blob/master/sys/dev/ic/bwfm.c)
sends a synthesized beacon through its common input path.
[NetBSD bwfm](https://github.com/NetBSD/src/blob/trunk/sys/dev/ic/bwfm.c)
and its native beacon consumer explain the legacy admission differences.
[FreeBSD scan methods](https://github.com/freebsd/freebsd-src/blob/main/sys/net80211/ieee80211_scan.c)
separate software scanning from optional driver scan operations.
