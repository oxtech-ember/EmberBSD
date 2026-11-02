# ESP32-S31 station Wi-Fi

`esp_wifi_if.c` presents the ESP32-S31 full-MAC radio as an Ethernet and
net80211 station interface. The radio firmware owns 802.11 management and
association frames. The OS path is intended to use the normal BSD
`wpa_supplicant`/net80211 EAPOL, key, and controlled-port interfaces.

## Experimental WPA2 and SAE/PMF path

`ESPWIFI_NET80211_WPA2` is an explicit experimental kernel option and remains
off in the default ESP32-S31 configurations. The optional
`ESP_MOSAICO_SC101IOT_WIFI` profile enables it only for the assembled Mosaico
SC101IOT kit; its `EXTRA_OBJ` must be supplied through `ESPWIFI_OBJECT` from
the output of `wifi/mkblob.py --external-wpa2`. Keep this profile out of other
board configurations. The historical option name is retained, but the
published source bridges WPA2-PSK and external SAE/PMF through the ordinary
BSD supplicant and net80211 APIs. This is source support; association,
protected-management behavior, and key lifecycle have not passed a hardware
trial.

The adapter accepts a WPA2-PSK/CCMP profile or SAE with PMF required. SAE
commit/confirm construction and parsing are bridged through the version-pinned
IDF callback table to the BSD external-SAE interface; the supplicant retains
authentication policy and cryptography. IGTK operations use the BSD key
interface. Since IDF 6.1 has no per-slot delete contract, deleting an installed
IGTK closes the data path and tears down the station context. This invalidates
the join/key generation; it does not claim physical zeroization of vendor
memory. A version-pinned IDF
`wpa_sta_rx_mgmt` callback receives successful association/reassociation
responses before EAPOL; the adapter checks status and selected BSSID before
publishing net80211 RUN. The later `WIFI_EVENT_STA_CONNECTED` is not treated
as the association boundary. RX and TX allow only EAPOL until the supplicant
authorizes the controlled port.
A vendor `STA_STOP` invalidates the active join and installed-key generation.
The adapter waits for that event, calls `esp_wifi_start()`, and keeps joins
fenced until the next `STA_START` event arrives. Old key requests are
drained by the control worker and rejected by their generation before a new
join can install keys. Each event wait is bounded at two seconds; failure leaves
the radio fenced without an automatic retry.

The BSD `cs_key_set` callback runs in ioctl process context. It copies the
key material into a serialized request and waits for the existing radio
control kthread to call IDF's key API; that kthread owns the required FPU/PCU
state and may sleep in the vendor routine. A request whose join generation is
stale is rejected before the vendor call, and any install failure closes the
port and queues teardown. The ioctl wait is bounded at 30 seconds; timeout
cancels a queued request or marks an in-flight request abandoned for secure
cleanup when it returns. A vendor call that never returns can still wedge the
radio control kthread, while the closed data port and ioctl timeout keep the
failure contained.

General software AUTH and ACTION management-frame forwarding is not supported;
SAE uses the dedicated vendor callback path. The adapter does not advertise
RRM/BTM roaming support. Ordinary reconnect remains the responsibility of
the common BSD supplicant path; 802.11k/v roaming is not integrated or tested.
Do not lower a profile's security requirements to make it match this path.

The selected producer uses a version-pinned `wpa_funcs` table without
`esp_supplicant_init()`, `wpa_sm`, or `eloop`. The table hands association to
IDF's `esp_wifi_sta_connect_internal()` and delivers EAPOL to the ordinary
BSD supplicant path. Its callback-table layout and internal API signatures
are tied to the selected ESP-IDF source package; update and re-audit them
together. This remains experimental: host checks do not establish the
firmware callback timing, negotiated PMF, protected-management behavior, or
WPA2/SAE association on ESP32-S31 hardware. Roaming and reconnect behavior
also remain unverified on hardware.

## Radio object producer

`wifi/mkblob.py` builds into an explicitly selected output path. Supply the
ESP-IDF source package as `IDF_SOURCE`, the current common Ports
`wpa_supplicant` source package as `WPA_SOURCE`, the selected RV32 C sysroot,
the native LLVM prefix, and the target Binutils executable prefix. For
example:

```sh
"$HOST_PREFIX/bin/python3.15" sys/arch/riscv/espressif/wifi/mkblob.py \
    --external-wpa2 \
    --idf-source "$IDF_SOURCE" \
    --wpa-source "$WPA_SOURCE" \
    --sysroot "$RV32_SYSROOT" \
    --host-prefix "$HOST_PREFIX" \
    --target-binutils-prefix "$BINUTILS_PREFIX/bin/riscv32--netbsd-" \
    --output "$WIFI_BUILD/espwifi.o"
```

The producer checks the radio entry declarations against calls in the actual
kernel consumer sources, including disconnect, then writes the relocatable
object only after checking its inputs and external ABI boundary. It remains
unaccepted for target use until kernel
integration and physical trial. A kernel configuration must explicitly
include that object through `EXTRA_OBJ`; this does not enable
`ESPWIFI_NET80211_WPA2` by itself. Keep build inputs and outputs outside the
source tree and use the tool prefixes selected by the SDK.

The management adapter regression checks callback-table field offsets against
the selected `esp_wifi_driver.h`, then exercises the adapter with modeled OS
callbacks. It also compiles the selected shared WPA/RSN parser sources and
tests valid WPA, valid RSN, and malformed IEs under ASan/UBSan:

```sh
"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_mgmt.py \
    --cc "$HOST_PREFIX/bin/clang" \
    --idf-driver-header "$IDF_SOURCE/components/wpa_supplicant/esp_supplicant/src/esp_wifi_driver.h" \
    --wpa-source "$WPA_SOURCE/src"
```

The adapter forwards association responses and rejects general AUTH and ACTION
management-frame callbacks; its dedicated SAE callbacks are separate. Host
tests do not establish kernel integration or radio behavior on hardware.
