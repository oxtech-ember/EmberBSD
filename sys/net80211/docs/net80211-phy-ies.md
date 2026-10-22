# Bounded HT/VHT scan observations

The native scan path retains four observed information elements:
HT Capabilities, HT Operation, VHT Capabilities and VHT Operation.
Their bodies must contain exactly 26, 22, 12 and 5 bytes.
The fixed node snapshot holds at most 73 bytes, including four TLV headers.
It requires no optional-IE allocation.

This is observation storage and scan export. Beacon and probe-response
observations are unauthenticated. They are not negotiated peer state.
The change adds no local capability advertisement, HT/VHT rates, WME flag,
A-MSDU enablement, block acknowledgement, channel-width policy or PHY programming.
Association-response negotiation remains separate work.

## Native consumers and lifetime

The existing beacon/probe-response consumer parses the complete IE window
into the scan parameters. The parser preserves the wire order of recognized
IEs and ignores complete unrelated IEs. A wrong fixed length, duplicate PHY
IE or truncated TLV clears the entire new snapshot. PHY parsing failure
adds no frame rejection. The existing legacy frame checks still apply.

`ieee80211_add_scan` replaces the node snapshot on each accepted scan update.
An update without PHY IEs clears previously observed PHY IEs.
The node uses the existing `ni_scan_epoch` and `ic_scan.nt_scan_epoch` owner.
A new epoch suppresses only stale PHY export. Existing legacy cache visibility
remains unchanged. A fresh scan update records the current epoch.
Node cleanup clears observations on leave or reuse. Reset allocation starts
with an empty snapshot and `copy_bss` does not copy observations from an old BSS.

Rekey does not clear observations. The data describes a received advertisement,
not a key or authenticated association. No crypto or key-install path reads or
writes this storage. Future negotiated HT/VHT state needs its own association,
key, reorder and teardown rules. It must not use this snapshot as authorization.

The existing `SIOCG80211` ioctl with `IEEE80211_IOC_SCAN_RESULTS` exports
these raw IEs through its unchanged scan-result layout. Its IE length field remains eight bits. The serializer
first reserves the original legacy IE bytes. It appends the whole PHY snapshot
only if it fits the remaining 255-byte budget. It never replaces RSN/WME bytes
to make room. The previous policy for a legacy IE set larger than 255 bytes
is unchanged. Scan record padding remains zeroed.

The internal node layout changes. Rebuild the kernel and dependent modules
together before target testing. No existing module ABI compatibility is claimed.
The kernel file list and rump net80211 library both select the new source.

## Primary source and checks

The fixed wire IDs and lengths follow the HT and VHT structures in
[FreeBSD ieee80211.h at d2145e9b96448da34d3cfede24c509af6d830c92](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211/ieee80211.h).
This bounded parser, storage policy and scan integration are original,
AI-assisted OpenAI Codex work. No upstream negotiation code is imported.

```sh
CC=gcc-16 sh ember/tools/net80211-phy-ies-parser-contract.sh
CC=gcc-16 sh ember/tools/net80211-phy-ies-contract.sh
CC=gcc-16 PHY_IES_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
    sh ember/tools/net80211-phy-ies-parser-contract.sh
CC=gcc-16 PHY_IES_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
    sh ember/tools/net80211-phy-ies-contract.sh
CC=gcc-16 sh ember/tools/net80211-phy-ies-analysis.sh
SDKROOT=$(xcrun --show-sdk-path) CC=gcc-16 sh ember/tests/bwfm-scan-padding.sh
CC=gcc-16 sh ember/tools/rtw88-net-contract.sh
```

The parser contract checks all 256 length values for each recognized IE,
all truncated window boundaries, duplicates, replacement and export budgets.
It passes 2210 assertions. The native contract executes complete current
`ieee80211_add_scan`, `node_alloc`, `node_cleanup`, optional-IE save functions,
scan-result construction and scan copyout bodies. It uses actual frame and
scan-parameter layouts. Kernel allocation, node lookup, rate setup and key
cleanup services remain explicit host models. It passes 45 assertions, including independence from borrowed scan parameters.
The whole beacon consumer is not executed by this host fixture.

The baseline native serializer failed four behavioral assertions before the
new export. The baseline scan storage/cleanup consumers failed two behavioral
assertions before their lifetime hooks. GCC16 strict ASan/UBSan, GCC path
analysis, Cppcheck and Clang Static Analyzer pass on these host contracts.
The prior scan-padding contract passes 28 assertions; the unchanged RTL8822CE
legacy net policy and native rate contracts pass 149 and 30 assertions.
Valgrind is unavailable on this macOS arm64 host. No Valgrind result is claimed.
Host models do not establish a complete target kernel/rump build, actual
supplicant behavior, HT/VHT negotiation or physical radio acceptance.

## Target boundary

The GCC16.2 aarch64 kernel at `560101dfe575d50eda9ee90f8665572062e74c01`
builds with `EMBER64_CIX_PERIPHERALS` and boots on CIX P1.
The installed `ifconfig -v rtwce0 list scan` exports all four observed IE types.
Legacy WPA2-PSK/CCMP, DHCP ACK, IPv6 link-local and 8 MiB in each direction pass.
The transferred payload matches SHA256 in both directions.
The new WME hardware consumers remain dormant in this legacy profile.
This check does not accept HT/VHT negotiation, QoS traffic or aggregation.
RF calibration warnings and long-run reliability remain open.

The pinned Linux workflow `.github/workflows/net80211.yml` runs source contracts
under GCC16, Memcheck, ASan/UBSan and static analyzers.
Local macOS GCC16 ASan/UBSan passes with leak detection disabled.
Local leak detection hangs inside LSan during process exit on macOS 27.
That run is incomplete. Linux leak detection remains enabled in CI.
