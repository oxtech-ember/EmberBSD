# ESP32-S31 station Wi-Fi

`esp_wifi_if.c` presents the ESP32-S31 full-MAC radio as an Ethernet and
net80211 station interface. The radio firmware owns 802.11 management and
association frames. The OS path is intended to use the normal BSD
`wpa_supplicant`/net80211 EAPOL, key, and controlled-port interfaces.

## Experimental WPA2 path

`ESPWIFI_NET80211_WPA2` is an explicit experimental kernel option and remains
off in the default ESP32-S31 configurations. The optional
`ESP_MOSAICO_SC101IOT_WIFI` profile enables it only for the assembled Mosaico
SC101IOT kit; its `EXTRA_OBJ` must be supplied through `ESPWIFI_OBJECT` from
the output of `wifi/mkblob.py --external-wpa2`. Keep this profile out of other
board configurations. The association path remains experimental until its
ABI checks and a separate target WPA2 hardware trial are complete.

The adapter currently accepts one WPA2-PSK/CCMP RSN profile for an initial
join after `WIFI_EVENT_STA_START`. It does not send software management
frames. A version-pinned IDF `wpa_sta_rx_mgmt` callback receives successful
association/reassociation response bodies before EAPOL; the adapter checks
the status and selected BSSID before publishing net80211 RUN. The later
`WIFI_EVENT_STA_CONNECTED` is not treated as the association boundary. RX
and TX allow only EAPOL until the supplicant authorizes the controlled port.
A vendor `STA_STOP` permanently fences further joins for the running adapter
instance; no event-queue drain guarantee is assumed.

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

SAE, WPA3, PMF, roaming/reconnect after `STA_STOP`, and vendor key deletion
are unsupported. Deleting an uninstalled key slot is a no-op; requesting
deletion of a tracked installed key fails closed, closes the data port, and
initiates teardown because the selected IDF version has no confirmed key
deletion contract. Do not lower a profile's security requirements to make it
match this path.

The selected producer uses a version-pinned `wpa_funcs` table without
`esp_supplicant_init()`, `wpa_sm`, or `eloop`. The table hands association to
IDF's `esp_wifi_sta_connect_internal()` and delivers EAPOL to the ordinary
BSD supplicant path. Its callback-table layout and internal API signatures
are tied to the selected ESP-IDF source package; update and re-audit them
together. This is still experimental: host checks do not establish the
firmware's full callback timing on the board or successful WPA2 operation.
The ESP32-S31 hardware WPA2 trial has not yet been performed.

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

The host regression scripts under `sys/arch/riscv/tests/t_espwifi_*.py`
exercise actual adapter functions with modeled OS and vendor callbacks. They
do not establish kernel integration or radio behavior on hardware.
