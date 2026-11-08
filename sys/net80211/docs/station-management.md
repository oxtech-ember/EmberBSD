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
