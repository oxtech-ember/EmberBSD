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
BSD supplicant and net80211 APIs. Current board support and hardware limits
are recorded in the [Mosaico Wi-Fi evidence](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp-mosaico/evidence/wifi-association.json).

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
authorizes the controlled port. After that authorization, the adapter submits
IDF's connection-complete handoff through the bounded radio worker, after the
current join's key readiness checks pass. Completion is recorded per join
generation; a local port revoke closes traffic but does not submit duplicate
completion if the same join is authorized again. WPA policy remains in the
common supplicant.

IDF's encrypted-profile selector rejects an empty STA password before it calls
the external authentication callbacks. This producer therefore writes the
fixed non-secret `bsd-external-auth` marker into that vendor ABI field. BSD
retains the actual credential and performs WPA/SAE authentication and key
derivation; the marker is not a password or fallback credential. The producer
rejects IDF's native `wpa.c` and `esp_wpa_main.c` sources and their password/PMK
entry points in this external-auth object. This boundary covers only the
validated WPA2-PSK/CCMP and SAE-with-required-PMF paths below; it does not claim
native WPA, FT, SAE-PK, or DPP support.

A vendor `STA_STOP` invalidates the active join and installed-key generation.
The adapter waits for that event, calls `esp_wifi_start()`, and keeps joins
fenced until the next `STA_START` event arrives. Old key requests are
drained by the control worker and rejected by their generation before a new
join can install keys. Each event wait is bounded at two seconds; failure leaves
the radio fenced without an automatic retry.

The teardown barrier also covers publication of the final net80211 state;
only then may a waiting SAE configuration admit the next join. Accepting AUTH
retires the preceding scan generation and queued scan work. A native scan
already running finishes in the serialized radio worker before the new connect
call, while its stale results and completion cannot update the new session.

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

"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_debug.py \
    --cc "$HOST_PREFIX/bin/clang" --analyze

"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_rx_observe.py \
    --cc "$HOST_PREFIX/bin/clang" --analyze

"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_ioctl_reset.py \
    --cc "$HOST_PREFIX/bin/clang"

"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_port.py \
    --cc "$HOST_PREFIX/bin/clang"
```

The adapter forwards association responses and rejects general AUTH and ACTION
management-frame callbacks; its dedicated SAE callbacks are separate. Host
tests also cover Ethernet multicast-reset handling and controlled-port ingress
and transmit behavior. Host tests do not establish kernel integration or radio
behavior on hardware.

The WPA2 adapter regression links the selected common WPA parser and checks
20, 22, 24, and 28-byte PSK RSNEs, PMF and cipher policy, and app-IE cleanup
under ASan/UBSan. EAPOL ingress checks that correctly framed EAP,
Start, Logoff, and Key packets reach the common supplicant while malformed
frames and controlled-port failures retain their bounds and fail-closed path:

```sh
"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_wpa2.py \
    --cc "$HOST_PREFIX/bin/clang" \
    --idf-driver-header "$IDF_SOURCE/components/wpa_supplicant/esp_supplicant/src/esp_wifi_driver.h" \
    --wpa-source "$WPA_SOURCE/src"

"$HOST_PREFIX/bin/python3.15" -B sys/arch/riscv/tests/t_espwifi_eapol.py \
    --cc "$HOST_PREFIX/bin/clang"
```

For one bounded vendor association trace, the radio producer accepts
`--vendor-debug-once`. It raises the vendor Wi-Fi log level to VERBOSE
immediately before the first validated external-WPA connect attempt, then
restores WARNING on connect, disconnect, stop, or synchronous connect failure.
The option is off by default and does not change security configuration.

For receive-path diagnosis, add `--vendor-rx-observe-once` to that option.
During the same first attempt it temporarily enables the existing promiscuous
callback for management and data frames, then restores its previous mode and
filter. Callback counts are recorded before the RX-status, length and address
filters, with failed RX and short buffers counted separately for management
and data callbacks. Successful data frames then pass successive counters for
any transmitter, the selected AP, FromDS direction and this station's address.
Association-response destination counters check that the observer's station
address agrees with the received association. The final snapshot also reads
the current promiscuous mode and filter, with API result codes, before restoring
the saved settings. It records only counters,
EAPOL type and key-information bits; no frame bodies, keys or addresses are
logged. Encrypted data and A-MSDUs are counted separately without parsing their
payloads. This observer can affect reception and timing, so its counters do not
establish normal-mode delivery or a working connection. The underlying mode
and callback constraints are described in the
[ESP32-S31 sniffer documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-guides/wifi-driver/wifi-modes.html#wi-fi-sniffer-mode).

For a receive-path probe that leaves the radio mode and filter unchanged, add
`--vendor-rx-chain-once` instead of `--vendor-rx-observe-once`. It counts entry
into the selected ESP32-S31 `sta_input`, crypto decapsulation, Ethernet
decapsulation, and `sta_rx_eapol` ROM-table slots during the same bounded debug
window, then emits one summary when the window ends. The producer verifies
each wrapper remains installed in its pinned table slot and tail-chains to the
matching ROM entry. At runtime, the probe snapshots the live table pointer and
checks those four slots at the beginning and end of the window. Its summary
reports pointer changes and both match masks; a mismatch also reports the
observed entry words.

The normal station RX route uses a separate `pTxRx` callback table. On the
pinned S31 path, `wifi_create_sta` registers `sta_rx_cb` at ifidx 0, which is
stored in `pTxRx + 0x3f8`; `ppRxPkt` calls it, and `sta_rx_cb` forwards the
packet to ROM `sta_input` directly. This route bypasses the `net80211_funcs`
`sta_input` slot wrapper. The probe therefore also verifies and snapshots the
`pTxRx + 0x3f8` entry, counts `sta_rx_cb` entries, and counts only the four
frame-control type values (management, control, data, extension) after the
pinned ESF/header resolution. It does not log packet contents, addresses, or
payload. A zero `sta_input` table count alone does not imply no normal STA RX;
the `sta_rx_cb` count is the relevant callback boundary. A zero callback count
still does not establish that the AP sent a frame or that it reached the PP
dispatcher. The FC type counts show only that a frame pointer reached this
callback and passed the pinned header lookup; they do not establish delivery
of EAPOL or a completed handshake.

The chain probe also wraps the S31 `ppRxProtoProc` entry in `pp_wdev_funcs` at
`+0x198`. ROM `ppRxPkt` calls that slot with the ESF and RX-control pointers
before checking the per-interface `rxmatch0` routing bit. The wrapper counts
the four FC types crossed with `rxmatch0` clear/set, plus null-pointer and
zero-MPDU-length cases, then tail-chains without changing arguments. The FC
lookup follows the same ESF indirections and optional eight-byte adjustment
that the pinned ROM callback uses. `rxmatch0` is the documented MAC-v3
interface-0 match bit. These counts cover frames reaching this PP processing
point after queueing and an earlier status check; they do not count all
over-the-air receptions or prove EAPOL delivery. A DATA count with
`rxmatch0` clear shows it reached PP processing but did not match the STA
interface. No count cannot distinguish an earlier radio/driver drop from no
on-air DATA frame.

The one-shot probe also wraps `get_sublen_offset` in `pp_wdev_funcs + 0x414`.
It calls the ROM helper once and observes only its call from the pinned
`wDev_ProcessRxSucData` return address (`0x2f839462`); other callers tail-chain
without instrumentation. At this earlier successful-RX processing boundary,
the wrapper validates the current descriptor and raw-buffer identity, bounds
the returned offset against the descriptor length, and reads only the metadata
length/status and first frame-control byte. It reports FC type crossed with
`rxmatch0`, DATA `rx_state` buckets, and invalid/short guards. The live
`pp_wdev_funcs` snapshot includes both the helper slot and the
`+0x1f4` `wDev_ProcessRxSucData` alias binding. These values locate frames that
reached this helper call; a stable table binding alone does not prove that the
selected caller executed, and zero counts do not establish absence of
over-the-air DATA.

For the same window, the probe records read-only interface-0 MAC and BSSID
register words at begin, after an accepted successful association response,
and at end, comparing them with the software STA MAC and expected BSSID where
available. It also records a read-only word from S31 MAC RX-policy register
`0x201040d8`; the pinned IDF `hal_mac_rx_set_policy` implementation performs
read/modify/write operations on that register. These values are register
readback only: they do not prove packet-filter configuration or packet
reception. The diagnostic does not write these registers or alter radio
policy.

For an opt-in legacy-PHY comparison, add `--vendor-legacy-phy` together with
`--vendor-rx-chain-once`. Before the radio starts, this diagnostic sets the
station protocol bitmap to 802.11b/g (`WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G`),
reads it back, and aborts initialization if the setter, getter, or bitmap
check fails. The default producer output keeps the existing PHY selection.
This diagnostic setting is runtime-only in the selected ESP32-S31
configuration and remains in effect until reboot; it is not restored when the
RX-count window closes, a permanent workaround, or a band-mode change.

The probe also samples the S31 hardware RX statistics
through the pinned IDF getter at successful radio start and at the window
boundaries. It reports modulo deltas at the width used by the selected S31
getter. The fields `nrx_err_pwrdrop`, `brx_err_agc`, `brx_err`, `nrx_err`,
`nrx_err_abort`, `nrx_err_agcexit`, `nrx_err_bboff`, `nrx_err_fdm_wdg`,
`nrx_err_restart`, `nrx_err_serv`, `nrx_err_txover`, `nrx_err_unsupport`,
`nrx_err_heformat`, `nrx_htsig_err`, `nrx_heunsupport`, and
`nrx_hesiga_crc` are masked to 10 bits by the getter, so their deltas wrap at
1024. The other fields below are copied as 16-bit values and wrap at 65536.
The output retains the vendor field names:
`rx_mpdu`, `rx_end_cnt`, `rx_datasuc`, `rx_fcs_err`, `rx_abort`,
`nrx_err_pwrdrop`, `rx_buf_fullcnt`, `rx_fifo_ovfcnt`,
`rx_abort_fcs_pass`, `rx_samebm_errcnt`, `rx_other_ucast`,
`rx_lastunmatch_err`, `rx_tkip_errcnt`, `rx_btblock_err`, `rx_freqhop_err`,
`rx_ack_int_cnt`, `rx_rts_int_cnt`, `brx_err_agc`, `brx_err`, `nrx_err`,
`nrx_err_abort`, `nrx_err_agcexit`, `nrx_err_bboff`, `nrx_err_fdm_wdg`,
`nrx_err_restart`, `nrx_err_serv`, `nrx_err_txover`, `nrx_err_unsupport`,
`nrx_err_heformat`, `nrx_htsig_err`, `nrx_heunsupport`, and
`nrx_hesiga_crc`. It does not clear counters or change radio mode or filters.
The pinned vendor header does not document the increment conditions or frame
types for these fields. Treat each delta as raw hardware-stat activity only:
it does not establish receipt of a particular 802.11 frame or EAPOL. If the
radio is stopped before a snapshot, the hardware delta is reported
unavailable. Allocator output reports internal-pool request accounting,
including initialization totals and window boundary totals; it does not
identify whether a caller's field was `_internal` or `_wifi_*`. This option
requires `--vendor-debug-once` and is off by default.
