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

| Capability | Station TX | Station RX |
| --- | --- | --- |
| `IEEE80211_STA_ACTION_BTM` | BTM query and response | BTM request |
| `IEEE80211_STA_ACTION_NR` | Neighbor report request | Neighbor report response |
| `IEEE80211_STA_ACTION_RM_REPORT` | Measurement report | None |
| `IEEE80211_STA_ACTION_RM_REQUEST` | None | Measurement request |

Both current adapters select BTM, neighbor reports and measurement reports.
Only the AIC adapter selects measurement request reception.
The shared code has no chip names or firmware-version tests.

BTM termination duration and URL fields must fit when their request flags are set.
An accepted BTM response must include its target BSSID.
Trailing IEs, neighbor-report fixed fields and nested subelements must be complete.
Measurement IEs need token, mode and type. Their type-specific data remains opaque.
Unknown IEs are preserved for the station client if their framing is valid.

Syntax validation does not authorize a frame or prove firmware support.
The adapters still enforce peer addresses, association generation, key state,
PMF authentication, replay protection, and the existing ioctl authorization.
Firmware command formats, status handling and lifecycle stay in each driver.

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
generation and BSSID; its v2 `freq` field is zero. The completion event still
contains the original SCAN frequency. A cancelled operation cannot be replaced
until its adapter has drained it. Both bwfm and aicwf advertise the capability
and use these common validation and event-lifetime helpers.
The bwfm adapter preserves the old firmware sync ID through cancellation, queues
the legacy S_SCAN transition behind that drain, and retires it at a terminal
event or successful firmware DOWN acknowledgement. The aicwf adapter keeps its
single native scan busy until the firmware completion indication.

## Software checks

Use the selected native SDK compiler and shell:

```sh
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
