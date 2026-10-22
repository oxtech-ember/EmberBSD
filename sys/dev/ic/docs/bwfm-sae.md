# External SAE in bwfm

EmberBSD connects upstream wpa_supplicant 2.11 SAE and its host four-way
handshake to the CYW43455 external-authentication firmware interface. The OS
repository owns the client, driver ABI and tests. On 2026-10-08 a physical
[CM5 on Waveshare CM5-NANO-B](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/README.md)
passed SAE group 19/H2E, required PMF, bidirectional file transfer, reconnects,
wrong-password rejection, a WPA2 regression and startup after reboot.

The tested station mode uses CCMP and required PMF with BIP-CMAC-128. This
is not Wi-Fi certification or validation on every bwfm radio. It does not add
fast transition, autonomous firmware roaming, 802.11v policy, SAE password
offload or multi-link operation. Other SAE groups/modes need separate checks.
Existing WPA2 connections keep their host handshake. Unsupported radios,
old kernels and clients built without CONFIG_SAE do not advertise SAE.

## Built-in radio limits

The built-in CYW43455 on Raspberry Pi 5 and wireless CM5 supports
802.11ac, one spatial stream (1x1), and channel widths up to 80 MHz.
These are hardware limits. OS, UEFI, and radio firmware updates cannot
add 802.11ax or a second spatial stream. The CM5 external antenna connector
selects an alternative antenna; it does not provide a second MIMO chain.
See the [CYW43455 specification](https://www.infineon.com/part/CYW43455)
and [CM5 datasheet, Wireless](https://datasheets.raspberrypi.com/cm5/cm5-datasheet.pdf).

802.11k radio measurements and 802.11v BSS transition management are
separate from the PHY generation and stream count. An AP capability label
does not validate their complete operation. The published driver disables
firmware WNM transitions. A separate [host-managed station transport](bwfm-kv.md)
implements Neighbor Reports and BSS Transition Management. A physical Pi 5
passed Neighbor Reports, BTM acceptance/rejection and traffic under WPA2 and
SAE/required PMF. The linked guide identifies the tested mesh and limits.
802.11ax 2x2 needs another radio, two antenna paths, and a supported host
interface, driver, and firmware. No such adapter is validated by this guide.

## Configuration and verification

Install the updated kernel, matching modules and base wpa_supplicant together.
Preserve a working Ethernet path and a rollback copy before radio testing.
An access point offering SAE and PMF is required. A transition-mode access
point can be used, but the test client must permit only SAE:

```conf
ctrl_interface=/var/run/wpa_supplicant
sae_pwe=2
network={
    ssid="YOUR-SSID"
    key_mgmt=SAE
    ieee80211w=2
    pairwise=CCMP
    group=CCMP
    sae_password="YOUR-PASSWORD"
}
```

Keep `/etc/wpa_supplicant.conf` owned by root with mode 0600. The password
must be the original passphrase; a WPA2-derived PSK is insufficient. Keep
private profiles and logs out of source control. Never enable supplicant
key logging to collect public evidence.

Use the normal NetBSD wpa_supplicant service with the BSD driver and `bwfm0`.
After association, inspect:

```sh
wpa_cli -i bwfm0 status
sysctl -w hw.bwfm0.report=1
dmesg | tail -100
```

The accepted CM5 connection reports `wpa_state=COMPLETED`, `key_mgmt=SAE`,
`pmf=1`, `mgmt_group_cipher=BIP`, `sae_group=19` and `sae_h2e=1`.
On the tested firmware the driver report also shows `wsec=0x604`,
`wpa_auth=0x40000` and `mfp=2`. Verify the firmware state after traffic-key
installation: a successful SAE exchange alone does not prove PMF stayed on.
Raw interface diagnostics may expose keys; redact private logs before sharing.

If a capable station uses WPA2, check its active profile before replacing the kernel:

```sh
wpa_cli -i bwfm0 status
wpa_cli -i bwfm0 get_capability key_mgmt
# Replace NETWORK_ID with the id reported by status.
wpa_cli -i bwfm0 get_network NETWORK_ID key_mgmt
wpa_cli -i bwfm0 get_network NETWORK_ID ieee80211w
```

A profile that permits only `WPA-PSK WPA-EAP` cannot select SAE.
Installing a new kernel does not rewrite that profile.
A reported client capability does not prove that the active profile enables it.
Use the explicit SAE profile above and verify the resulting SAE and PMF status.

If Ethernet and Wi-Fi share a subnet, an SSH connection to the Wi-Fi address
alone does not establish the return path. Check the route to the test peer
and verify it uses bwfm0 in both directions. Do not disable the management
interface just to establish that route.

## Interface and security boundaries

`IEEE80211_IOC_SAE` is additive. Version 1 uses fixed-width fields, a bounded
1536-byte payload, a peer address and a userspace association generation.
The getter returns capabilities only. Configuration, frame transmission,
authentication status and IGTK operations require interface administration
privilege. Authentication traffic is accepted only for the active peer and
generation while the interface is authenticating. Stale status cannot
authorize a new association. Passwords and PMKs remain in userspace.

`RTM_IEEE80211_SAE` forwards external-auth requests and received authentication
frames. The supplicant checks their sizes, peer and generation before passing
them to its upstream SAE implementation. Firmware provides CCMP and PMF;
the driver installs management keys at IGTK indexes 4 and 5.

CYW43455 firmware 7.45.265 advertises `sae_ext`, not full SAE offload.
Its external-auth request flags can be zero; the event type identifies the
request. The driver accepts unsupported `sup_wpa=0` when firmware has no
internal supplicant. Other security-setup failures abort the association.
Changing PMF can require the radio down. Setting `wsec` clears PMF, so the
driver sets and verifies PMF last, then preserves it while installing keys.
Firmware commands are serialized through their response, and BCDC identifiers
wrap to their 16-bit wire width. Disconnect also aborts the firmware join.

## Reconnecting with external SAE

The BSD external-SAE path starts a new SAE exchange on each join. Reusing a
cached PMKID in the association RSNE can therefore refer to the previous PMK
while firmware requests fresh authentication. On a CM5 mesh test this left
a repeated connection to the same BSSID waiting for association; clearing
the client's PMKSA cache restored it.

The client now skips cached-PMKSA selection for SAE-enabled profiles when
the driver advertises the external-SAE/PMF interface. It still installs the
PMK obtained by the new SAE exchange. Pure WPA2 profiles and other driver
backends retain their existing cache behavior. A mixed WPA2/SAE profile on
this driver also skips reuse; use a separate WPA2-only profile if that cache
behavior is required. This is full reauthentication, not fast roaming.

The cache-selection regression exercises the actual client block:

```sh
sh ember/tests/bwfm-sae-pmksa.sh
```

It failed before the fix and passed on macOS and an EmberBSD AArch64 VM.
On the CM5, three reconnects to one 5 GHz BSSID without flushing PMKSA
completed in 15, 14 and 14 seconds with SAE/H2E and required PMF. The fixed
client also connected using a separate WPA2 profile and returned to SAE.
See the [current board receipt](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/README.md).

## Build and checks

Build a matched kernel/modules bundle and base wpa_supplicant from the same
clean source revision using the [cross-build instructions](../../../../tools/docs/cross-build.md).
The client requires the existing OpenSSL-enabled build. Install its updated
net80211 headers into the target sysroot before compiling the client.
Do not combine a new client binary with an unrelated kernel acceptance claim.

The portable parser regression can run under address/undefined sanitizers:

```sh
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    ember/tools/bwfm-sae-contract.c -o /tmp/bwfm-sae-contract
/tmp/bwfm-sae-contract
```

Its 106 assertions cover RSN ciphers/PMF, truncation, management suites,
bounded PMKID counts, capability tokens and firmware request peer/SSID bounds.
Compile `ember/tools/bwfm-sae-ioctl.c` for NetBSD and run it as root on the
target interface with the supplicant stopped. Its 13 live checks deliberately
disable SAE and exercise lengths, versions, reserved fields, inactive status
and unprivileged requests. Restore the service after this test.

Hardware acceptance requires actual bidirectional traffic, disconnect/reconnect,
wrong-password rejection without WPA2 fallback, an independent WPA2 profile,
and automatic startup after reboot. The board receipt records completed
checks and limits; one board and one access point are not a compatibility matrix.

## Provenance

The external-auth firmware protocol follows Raspberry Pi Linux
[`brcmfmac`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/net/wireless/broadcom/brcm80211/brcmfmac),
specifically `cfg80211.c`, `fwil_types.h` and `fweh.h`. Those sources retain
Broadcom Corporation copyright (2010 and 2012) under ISC. EmberBSD implements
the protocol using its existing bwfm command transport and NetBSD interfaces;
the cryptographic SAE implementation remains upstream wpa_supplicant.
