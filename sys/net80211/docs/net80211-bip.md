# Bounded net80211 BIP-CMAC-128

[`ieee80211_bip.h`](../ieee80211_bip.h) owns the common, independent BSD
CMAC/BIP verifier. It is shared by the native station PMF path and the
[isolated AIC compatibility helper](../../dev/sdmmc/docs/aicwf-bip.md).
AIC8800D80 uses its firmware BIP path; moving this helper does not move that
driver's cryptography to the host. This source and its host contracts do not
establish a usable WPA3 connection or radio acceptance.

## Receive contract

`ieee80211_bip_verify()` takes an AES-128 block callback, its key context,
a complete MPDU without FCS, explicit expected transmitter and BSSID,
IGTK KeyID 4 or 5, and the initial or last accepted 48-bit IPN. The callback
must support distinct input/output buffers and cannot fail. Kernel callers
use the existing `aes_setenckey128()`/`aes_enc()` API. There is no private
AES implementation or kernel dependency on OpenSSL.

Frames are bounded to 1536 bytes, with a 24-byte management header and an
exact final 18-byte MMIE (element 76, length 16, 16-bit KeyID, six-byte
little-endian IPN and eight-byte MIC). The destination must be a group
address. The source and BSSID must match explicit, nonzero unicast addresses.
DS bits, fragmentation, the Protected flag and HT-control/optional headers
are rejected. Retry, Power Management and More Data are masked in the AAD;
duration and sequence number are excluded. Deauthentication/disassociation
require exactly a two-byte reason body. Actions require at least category
and action code; the caller checks their remaining category-specific fields.

A received IPN must strictly exceed the supplied floor. The verifier never
modifies the input MPDU or supplied key/replay state. It writes a candidate
IPN only after comparing all eight MIC octets successfully. Every failure
leaves the candidate unchanged. The caller must serialize key and association
lifetimes, commit the candidate under that lock, and preserve replay state
when the same key is reinstalled. Authentication does not replace recipient,
association-state or action-specific checks in the caller.

`ieee80211_is_robust_mgmt()` classifies a plaintext, ordinary-header frame.
The caller validates the header and decrypts protected unicast frames before
using its action category. Setting the Protected bit does not make a category
robust. Deauthentication/disassociation are classified even with incomplete
bodies, so malformed disconnects do not receive an authentication exemption.

The action classifier uses the upstream robust-category exclusions: Public
(4), HT (7), Unprotected WNM (11), Self-Protected (15), Unprotected DMG (20),
VHT (21), Unprotected S1G (22), and Vendor Specific (127). Category 23 is the
protected S1G category. Other, reserved and error category values
conservatively require authentication; this is not permission to dispatch an
unsupported category. New category handlers must separately establish their
IEEE protection rules before admitting unauthenticated frames.

This interface provides receive verification only. It does not sign group
management transmissions, manage IGTK lifetime, implement GMAC/CMAC-256,
protect beacons, or authenticate encrypted unicast management frames.

## Host checks

The contract compiles the actual common header with a selected native C11
compiler, `pkg-config`, and OpenSSL 3 or later. OpenSSL supplies real AES
blocks and a separate EVP_MAC CMAC oracle. The suite checks all four RFC
4493 known answers, every three-segment split through 64 bytes, fixed BIP
vectors, all bounded action-frame lengths, exact-sized truncation buffers,
all bit flips in an authenticated frame, initial/replayed/exhausted IPNs,
IGTK selection, peer identity and valid-MIC malformed headers/MMIEs.
Failures preserve both candidate replay state and frame bytes.

```sh
CC=/absolute/host-pkg-llvm/bin/clang \
PKG_CONFIG=/absolute/host-pkg-llvm/bin/pkg-config TMPDIR=/tmp \
NET80211_BIP_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' \
    /absolute/host-pkg-llvm/bin/mksh ember/tests/net80211-bip.sh
```

The existing `ember/tests/aicwf-bip.sh` still exercises the narrower AIC
compatibility contract with the same shared CMAC implementation. Its action
allowlist remains RRM/WNM, with a minimum three-byte body.

Both runners honor `TEST_RUNNER` for Memcheck. Set `NET80211_BIP_ANALYZE=1`
or `AICWF_BIP_ANALYZE=1` to run their Clang Static Analyzer and Cppcheck
passes; `CSA` selects Clang when it is not available as `clang` on `PATH`.

Clang Static Analyzer and Cppcheck can analyze each contract translation
unit with its normal include paths (`sys/net80211` for the shared suite,
`-idirafter sys -I sys/dev/sdmmc` for the compatibility suite so kernel system
headers cannot shadow native host headers). They need no diagnostic
suppressions. Use the host OpenSSL include flags for Clang. These host checks
do not replace a target kernel build, native runtime integration checks, or
hardware validation. Valgrind requires a supported execution host; ASan and
UBSan cover the macOS host contracts.

## Independently checked vectors

Both fixed vectors use key `2b7e151628aed2a6abf7158809cf4f3c`. The following
hand-serialized AAD/body byte strings include a zero MIC. The test stores
literal MPDUs and the first eight bytes of each full CMAC; its frame-builder
and signing helpers are not used to create these expectations.

| Frame | AAD/body (hex) | Full AES-CMAC |
| --- | --- | --- |
| Deauthentication, IGTK4, IPN42 | `c000ffffffffffff02030405060702030405060703004c1004002a00000000000000000000000000` | `d2d2aa05da32e95e5765dcd9f9eb9bed` |
| SA Query, IGTK5, IPN0x010203040506 | `d000ffffffffffff0203040506070203040506070800cafe4c1005000605040302010000000000000000` | `51772d63e3af2f622c97654827fa0472` |

These CMAC values were checked independently with OpenSSL's CLI `mac` command
using `-macopt cipher:AES-128-CBC` and `-macopt hexkey:<key>` on each decoded
byte string. The SA Query vector exercises cryptographic framing only;
a group SA Query need not be a permitted action for a particular station.

## References and provenance

- [RFC 4493, section 4: AES-CMAC test vectors](https://www.rfc-editor.org/rfc/rfc4493#section-4).
- [Linux upstream robust-management classifier and category values](https://kernel.googlesource.com/pub/scm/linux/kernel/git/wireless/wireless-next/+/8f7aa3d3c7323f4ca2768a9e74ebbe359c4f8f88/include/linux/ieee80211.h).
- [Hostap category values referencing IEEE 802.11-2020, 9.4.1.11, Table 9-51](https://android.googlesource.com/platform/external/wpa_supplicant_8/+/ab49c33aef291e4d9cabe9a16f535bca40569a10/src/common/ieee802_11_defs.h).

The CMAC/BIP core was promoted from the repository's independently implemented
AIC host preparation helper. Protocol category facts were checked against the
primary upstream references; no GPL implementation was copied. The promotion,
classifier and regression checks were prepared with AI assistance.
