# Bounded HE (802.11ax) station negotiation

[`ieee80211_he_ies.c`](../ieee80211_he_ies.c) owns the HE wire codec.
[`ieee80211_ht20.c`](../ieee80211_ht20.c) and
[`ieee80211_output.c`](../ieee80211_output.c) offer and commit HE beside the
HT20 and VHT contract of a station. The driver supplies only its local HE
capabilities and programs its hardware from the committed result.

## Wire values

`ieee80211_he_ies_parse(out, data, len)` reads a complete bounded TLV
window. It finds the HE Capabilities element (element 255, extension 35)
and the HE Operation element (element 255, extension 36), as defined in
IEEE Std 802.11ax-2021 9.4.2.248 and 9.4.2.249.

- HE Capabilities: 6 octets of MAC capabilities, 11 octets of PHY
  capabilities, the HE-MCS and NSS sets for 80 MHz and smaller, and the
  sets for 160 and 80+80 MHz when the width bits announce them. The PPE
  Thresholds follow when the PHY capability bit announces them; their
  length comes from their own NSTS and RU index fields.
- HE Operation: the 24-bit parameters, the BSS color octet and the Basic
  HE-MCS and NSS set, then the VHT Operation Information, the co-hosted
  BSSID indicator and the 6 GHz Operation Information when the parameters
  announce them.

An element whose length differs from the length its own bits announce
fails, as do a duplicate HE element and malformed framing. Other complete
elements, including other extension elements, are skipped. Failure clears
the output. The codec allocates no memory and grants no rate or channel
permission. The maps use the VHT two-bit coding; entry 2 means MCS 0-11,
and `ieee80211_vht_mcs_intersect()` intersects them.

`ieee80211_he_cap_encode()` writes a complete HE Capabilities element of a
local profile. It does not encode 160 or 80+80 MHz maps.

## Local profile and offer

A provider registers `ic_he_admit(ic, width, cap)` after
`ieee80211_ifattach()`. net80211 calls it when it queues an HT offer in an
association request. The width is 20 MHz without a VHT offer, or the width
of the VHT channel (40 or 80 MHz) that the same request offers.
`ieee80211_he_local_check()` accepts only:

- one or two streams with MCS 0-7, 0-9 or 0-11, with equal RX and TX maps;
- no MAC capability;
- PHY capabilities limited to the width bit (none at 20 MHz, the 5 GHz
  40/80 MHz bit at 40 or 80 MHz), LDPC reception and the nominal packet
  padding;
- no PPE Thresholds.

The request then carries the HE Capabilities element after the VHT
Capabilities. A supplicant element with the same identity in the optional
IEs refuses the HT offer, as for HT and VHT. `ic_ht20_diag.he_offers`
counts queued HE offers.

## Commit and limits

The association response commits HE only for a queued HE offer and only
when it carries both complete HE elements. A 40 or 80 MHz offer also needs
the committed VHT channel of the same width. The TX map is the local TX
map intersected with the peer RX map, and the RX map the reverse; if the
AP does not disable SM power save, the TX map keeps one stream, as for HT
and VHT. A missing or malformed HE element keeps the HT/VHT result: HE is
optional for the association. `ic_ht20_diag.he` counts committed HE
contracts. A reset of the HT contract also clears HE.

Beacons do not withdraw HE. A change of the BSS color or the HE Operation
in a beacon keeps the committed values until the station reassociates.
The Basic HE-MCS and NSS set is not enforced. The station offers no
multi-user, TWT or 160 MHz capability.

## Source checks

`ember/tools/net80211-he-ies-contract.sh` runs the codec with an observed
802.11ax/be AP element (160 MHz maps and 13 octets of PPE Thresholds),
every shorter and longer element, missing optional fields, duplicates,
encoding bounds and the local profile bit by bit.
`ember/tools/net80211-wme-contract.sh` runs the actual offer and commit:
the element in the request at 80 and 20 MHz, commit from a complete
response, and no commit for a response without HE, without HE Operation,
with a short element or with no common map, nor for an unqualified or
refused local profile.

[RTL8852BE](../../dev/pci/docs/rtwx.md#ht-and-vht) is the first provider.
