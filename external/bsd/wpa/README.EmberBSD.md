# Native WPA builds

The native `wpa_supplicant` target uses `EMBER_WPA_SOURCE` to select the
prepared `net/wpa_supplicant-source` tree from Ports. The default
`EMBER_WPA_PROFILE=full` retains the EAP and TLS methods of the regular build.
An opt-in `EMBER_WPA_PROFILE=personal` selects the WPA personal configuration,
with WPA-PSK, SAE, SME and protected management frames; it does not include
enterprise EAP methods or TLS. `EMBER_WPA_CRYPTO_BACKEND` defaults to
`openssl`; an experimental `wolfssl` selection uses the common target WolfSSL
prefix at `${DESTDIR}/usr/pkg` (or `EMBER_WOLFSSL_PREFIX`). The WolfSSL recipe
is GPLv3, so a binary built with that backend has different distribution
obligations from the default OpenSSL build. The full profile currently remains
OpenSSL-only. WPA's EAPOL four-way handshake remains part of the personal
profile; the optional IEEE 802.1X EAP state machine is excluded.

The BSD kernel ABI adapter is an explicit OS input:
`dist/src/drivers/driver_bsd.c`. Both supplicant and hostapd select this one
file through `WPA_BSD_DRIVER`, copying it to a build-private
`driver_emberbsd.c` so each program and architecture owns its object; other
WPA code and public driver interfaces
come from the immutable prepared Ports source. The adapter includes
`<driver.h>` through that source's include path, so the older reference header
beside the OS adapter cannot shadow the selected API. This permits OS ioctl
changes to reach native builds without modifying a prepared source receipt
or maintaining another copy of WPA protocol algorithms.

On NetBSD interfaces, the adapter reads connected-scan, BTM and RRM support
from separate `IEEE80211_STA_CAP_*` flags. Connected scans use the versioned
scan request and completion contract independently of BTM/RRM; management
frame events and capability advertisement remain gated by their own flags.
The `ember/tests/bwfm-sta.sh` fixture exercises this OS-selected path,
including scan-only dispatch, matching completions, cancellation and the
existing WPA profile restrictions.

The target interface and driver must independently support association, key
installation, SAE external authentication and protected management frames.
Building this profile does not establish that a particular driver supports
those operations. Network credentials are runtime configuration inputs.

Run `tests/personal-profile.py --bmake <native-os-make-wrapper> --source
<prepared-wpa-source>` to check profile selection. Use the SDK native OS
runner and the selected native tool prefix for builds and tests.
