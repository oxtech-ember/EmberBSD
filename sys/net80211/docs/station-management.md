# Station management validation

`ieee80211_rsn.h` and `ieee80211_sta.h` own station security and action syntax.
The helpers borrow input bytes. They allocate no memory and change no state.

## Selected security

`ieee80211_sta_rsn_valid(ies, len, sae, rsn_first)` checks the entire association IE stream.
It uses `ieee80211_assoc_rsn_parse` to find exactly one RSN element.
Duplicate RSN/RSNXE elements and malformed framing fail validation.
Complete unrelated elements can follow RSN. A false `rsn_first` also permits preceding elements.
Both firmware adapters set `rsn_first` because their security consumers require RSN first.

Both selected modes require one CCMP group cipher and one CCMP pairwise cipher.
The AKM must be PSK without PMF, or SAE with both PMF capability and requirement.
PSK can omit capabilities. A partial capabilities field is invalid.
Every optional PMKID count and list must fit the RSN element.
An explicit group management cipher must be BIP-CMAC-128.
The standalone SAE helper retains its existing element-view contract.

This closes the former PSK tail-validation gaps in both firmware drivers.
It also checks the full SAE stream before station management admission.
Firmware PMF support and successful key installation remain separate requirements.

## Actions and firmware capabilities

`ieee80211_sta_action_valid(body, len, to_ap, caps, max_body)` checks syntax.
`to_ap` is true for station TX and false for RX from its AP.
The caller selects supported action kinds and the firmware body-size limit.

| Action syntax capability | Station TX | Station RX |
| --- | --- | --- |
| `IEEE80211_STA_ACTION_BTM` | BTM query and response | BTM request |
| `IEEE80211_STA_ACTION_NR` | Neighbor report request | Neighbor report response |
| `IEEE80211_STA_ACTION_RM_REPORT` | Measurement report | None |
| `IEEE80211_STA_ACTION_RM_REQUEST` | None | Measurement request |

The STA GET ABI reports operation direction with separate capability bits:

| STA GET capability | Meaning |
| --- | --- |
| IEEE80211_STA_CAP_NR | Neighbor report request TX and reply RX are both available. |
| IEEE80211_STA_CAP_RM_REQUEST_RX | Measurement request reception from the AP is available. |
| IEEE80211_STA_CAP_RM_REPORT_TX | Measurement report transmission to the AP is available. |

AIC exposes NR in both directions and transports measurement requests and
reports. The measurement IEs remain opaque to the adapter; the capability
does not claim a measurement implementation. BWFM exposes NR in both
directions and measurement report TX. ESP exposes NR in both directions only.
BTM and connected scan keep their independent bits. The shared code has no
chip names or firmware-version tests.

BTM termination duration and URL fields must fit when their request flags are set.
An accepted BTM response must include its target BSSID.
Trailing IEs, neighbor-report fixed fields and nested subelements must be complete.
Measurement IEs need token, mode and type. Their type-specific data remains opaque.
Unknown IEs are preserved for the station client if their framing is valid.

Syntax validation does not authorize a frame or prove firmware support.
The adapters still enforce peer addresses, association generation, key state,
PMF authentication, replay protection, and the existing ioctl authorization.
Firmware command formats, status decoding and physical resource lifetime stay
in each driver.

## Action request lifetime

`ieee80211_sta_action.h` owns the logical lifetime shared by ESP, AIC and BWFM.
Its compact state holds the accepted cookie, association generation, BSSID and
frequency, plus cancellation and completion state. The adapter retains the
body, firmware token and any resources borrowed by its transport. This ABI is
version 3; tx_error is zero or a positive BSD errno for a local submission or
firmware completion error.

Call the helpers under the interface's existing serialization. begin admits
one operation; match checks its full identity. report prepares at most one
fixed STA_TX notice. A local error is carried in tx_error with ACK flags clear.
On local success, IEEE80211_STA_TX_ACK_KNOWN means the adapter knows the peer
ACK result; IEEE80211_STA_TX_ACK is valid only with ACK_KNOWN. Clear ACK with
ACK_KNOWN means a confirmed no-ACK result. With both bits clear, the peer ACK
result is unknown. Deliver that notice after dropping driver locks. Syntax,
capabilities and current association authorization are checked before
admission through the existing common validators and adapter state.

`CANCEL_TX` matches cookie, generation and BSSID. The BSD supplicant supplies
zero frequency; a supplied nonzero frequency must match the original request.
Cancellation and invalidation suppress later completion delivery. The
supplicant's timeout supplies its own failure notification. A cancelled
operation keeps its state until the adapter can safely call `release`; a
stale tuple cannot release its replacement.

Transport retirement depends on the adapter's firmware contract. ESP retains
its slot until the ordinary management output buffer drains or submission
proves that no buffer was accepted. AIC and BWFM correlate independent command
buffers with firmware tokens. AIC firmware confirmation distinguishes ACK
from no-ACK. BWFM maps SUCCESS and NO_ACK explicitly; timeout and other
firmware errors carry a local errno without inferring a peer result. ESP carries
the lower management-send result through its callback only after the vendor
request buffer drains; the vendor callback does not establish peer ACK. Each
adapter keeps correlation across late events and admits reuse only when its own
resources are safe. The common helper does not call firmware, acquire driver
locks or deliver route events.

## Connected scan offload

`IEEE80211_STA_CAP_SCAN` advertises scans that run while the station stays
associated. A driver reserves its own bounded work item before calling
`ieee80211_sta_scan_begin`; the common helper then validates the RUN association,
association generation, BSSID, current frequency and requested active channels,
sets `IEEE80211_F_SCAN`, and advances the common scan-cache epoch. The driver
owns the firmware command and the point at which its operation is physically
drained.

The helpers mutate common scan state and must be called in the interface's
serialized net80211 context (for example, under `splnet` where required by the
platform). They do not take driver locks or call firmware. Adapters snapshot
their request while holding their own lock, drop that lock, then call common
report/release in the serialized context; this avoids lock inversion through
route-event delivery.

The common scan tuple owns cookie, generation, original frequency and BSSID for
completion delivery. `ieee80211_sta_scan_report` emits at most one scan event
from that saved tuple, even after the association has left RUN. An aborted
report invalidates the cache epoch and clears the foreground scan flag, while
the common operation remains active until the adapter calls
`ieee80211_sta_scan_release` after its work can no longer touch hardware. A
failed admission before hardware ownership uses release as a silent rollback;
after an ioctl has been accepted, launch failures must report an aborted event
before release. A failed firmware abort does not end the drain.

`CANCEL_SCAN` identifies the saved request with its nonzero cookie, association
generation and BSSID; its request `freq` field is zero. The completion event
still contains the original SCAN frequency. A cancelled operation cannot be
replaced until its adapter has drained it. Both bwfm and aicwf advertise the
capability and use these common validation and event-lifetime helpers.
The bwfm adapter preserves the old firmware sync ID through cancellation, queues
the legacy S_SCAN transition behind that drain, and retires it at a terminal
event or successful firmware DOWN acknowledgement. The aicwf adapter keeps its
single native scan busy until the firmware completion indication.

## Host-MLME station management

`ieee80211_sta_soft.h` is the `ic_sta_mgmt` owner for a radio whose MLME
runs in net80211: management frames pass through `ieee80211_input()` and
`ieee80211_mgmt_output()`. The adapter calls `ieee80211_sta_soft_attach()`
after `ieee80211_ifattach()` and `ieee80211_sta_soft_detach()` before
`ieee80211_ifdetach()`. It supplies one operation, `sso_tune`.

GET reports `IEEE80211_STA_CAP_BTM` and `IEEE80211_STA_CAP_SCAN`. The
generation is the low 32 bits of the association epoch of `ic_bss`. The
supplicant sends ENABLE for an RSN association with CCMP and PSK, or SAE
with required PMF. Then:

- A BSS Transition Management Request of the BSS goes to the supplicant as
  `RTM_IEEE80211_STA_RX`. net80211 accepts only an unfragmented frame from
  the BSS to the station, with a complete body. With SAE, the shared PMF
  consumer has already decrypted and verified the robust Action frame.
  `ieee80211_sta_soft_rx_admit()` is the pure part of this rule.
- STA_TX queues a BTM Query or Response body through
  `ieee80211_send_action()`, the common PMF transport. The radio gives no
  ACK result to net80211, so the STA_TX notice follows at once with both
  ACK flags clear.
- STA_SCAN runs a passive connected scan. Before the radio leaves the
  operating channel, the station sends a Null frame with the Power
  Management bit set (IEEE Std 802.11-2024, 11.2.3), and data frames carry
  the bit while it is set. The station listens 120 ms on each channel, at
  most two channels per absence, then returns for 100 ms and sends a Null
  frame with the bit clear. The plan leaves out the operating channel: its
  beacons arrive between absences. The adapter passes frames of the listen
  channel to `ieee80211_sta_soft_rx_scan()`, which adds beacons and Probe
  Responses to the scan cache with `ieee80211_scan_observe()`.
- A state change out of RUN, DISABLE, CANCEL_SCAN or detach ends the scan
  with an aborted notice. The radio returns before the notice.
- An AP can keep sending during an absence and move its block-ack window
  by 2^11 or more; such MPDUs look old. After each return the owner calls
  `ieee80211_rxba_resync()`: the first MPDU outside the window starts it
  again, and a retransmission from the last window stays old.

`sso_tune(c)` moves the radio to the 20 MHz primary channel `c` and holds
station TX in its queues; `sso_tune(NULL)` restores the operating channel
and width. The call can sleep and runs from the common work queue with the
kernel lock. The adapter serializes it with its own state work, refuses a
tune while a state change waits, and ends a listen period before that work
changes the radio. The adapter also pauses its beacon-loss supervision.

A radio that reports the absolute signal level uses `ieee80211_input_dbm()`.
The scan cache then holds dBm for all entries of the radio. The supplicant
compares candidates with these values, so one radio must not mix dBm and
relative levels.

The owner sends no probe request and has no Neighbor Report or Radio
Measurement support. `tests/sta-soft.py` checks the admission rule. The
scan engine and the frame paths are checked on hardware only.

## Foreground scan requests

A stop or reset can move the station from SCAN to INIT before the last
channel. `ieee80211_abort_scan()` then still sends one
`RTM_IEEE80211_SCAN`, because the BSD supplicant waits for it without a
timeout and refuses each new scan until it arrives.
`tests/scan-abort.py` checks this rule.

A station scan request (`IEEE80211_IOC_SCAN_REQ` and the `wi` scan requests)
returns `EBUSY` in AUTH and ASSOC. These states normally hold an exchange: a
frame waits for its reply, or the driver firmware runs the join. With manual
roaming, a deauthentication or disassociation in RUN leaves the BSS and stops
in AUTH or ASSOC with no exchange. `ieee80211_newstate()` then sets
`IEEE80211_FEXT_STA_LEFT`, so the supplicant scan starts. More
deauthentication or disassociation frames keep the flag. A transition to
INIT, SCAN or RUN, or a new `ieee80211_sta_join()`, clears it.
`tests/scan-setup-contract.c` and `tests/sta-left.py` check these cases.

## Association lifecycle ABI

The versioned MLME request and BSS ASSOC/REASSOC/DISASSOC epoch payload are
specified in [association-lifecycle.md](../association-lifecycle.md). Kernel
and userland consumers must be upgraded together to use this ABI.

## Software checks

Use the selected native SDK compiler and shell:

```sh
export EMBER_WPA_SOURCE=/absolute/path/to/prepared-wpa-source
CC="$host_cc" "$sdk_sh" ember/tests/net80211-sta.sh
CC="$host_cc" "$sdk_sh" ember/tests/bwfm-sta.sh
CC="$host_cc" "$sdk_sh" ember/tests/aicwf-sta.sh
CC="$host_cc" "$sdk_sh" ember/tests/aicwf-sae.sh
CC="$host_cc" "$sdk_sh" ember/tests/aicwf-sae-lifetime.sh
```

The common test covers valid and malformed RSN, action direction, capabilities,
optional fields, IE framing, nested subelements and truncation boundaries.
Driver fixtures execute production adapters and retain authorization and lifecycle tests.
Set `CFLAGS` for common and bwfm diagnostics; AIC uses `AICWF_STA_CFLAGS`.
The common test also accepts `STA_TEST_CFLAGS` and `TEST_RUNNER` for CI Memcheck.
These tests do not establish full kernel integration or physical radio behavior.
