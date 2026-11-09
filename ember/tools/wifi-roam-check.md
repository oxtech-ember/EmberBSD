# Target Wi-Fi roaming check

`wifi-roam-check.sh` exercises one explicit same-ESS transition through the
installed `wpa_supplicant`. Run it on an already configured target with a
DHCP client and at least two compatible access points. The check is an optional
diagnostic; image recipes select it explicitly.

```sh
/bin/sh /path/to/wifi-roam-check.sh bwfm0 /tmp/roam-check 75 example.com
```

Arguments are the interface, a new result directory, an optional time budget
in seconds (default 75, maximum 300), and an optional DNS lookup name. The
parent of the result directory must exist. The script uses the target's base
`sh`, `date`, `sleep`, `cp`, `grep`, `wpa_cli`, `route`, `getent`, and `ping`.

The check waits for an authorized connection and an IP address, flushes unused
BSS cache entries, and requests one scan. It selects a different BSSID with
the same encoded SSID and compatible SAE or PSK capability, confirms the BSS
record, and immediately requests one ROAM. Cache freshness is the readiness
condition; the script does not claim to observe a scan-completion event.
An in-use BSS preserved by the flush is eligible when its age is consistent
with the scan window, allowing one second for age granularity. Thus a scan
that reconnects to the other AP can be followed by a move back. The current
BSSID is checked again before ROAM; this is an observation, not an atomic
reservation against automatic roaming by the supplicant.

PASS requires `COMPLETED` on the requested BSSID with the same SSID and key
management, an IP address, DNS resolution, and a gateway echo reply. A control
reply of `OK` alone is insufficient. Saved profiles remain in use, and the
script leaves the final association to the normal supplicant policy.

| Exit | Result | Meaning |
| --- | --- | --- |
| 0 | PASS | Transition and connectivity checks passed |
| 1 | FAIL | A precondition, control request, transition, or connectivity check failed |
| 2 | Setup error | Invalid arguments or result directory could not be created |
| 77 | SKIP | Unsupported test profile or no fresh compatible alternate BSS |

Stdout contains compact `WIFI_ROAM_CHECK` phase and result lines. The result
directory is private (umask 077) and contains the control replies, original and
final status, requested BSSID, and network checks. These files include network
identifiers; retain them as local diagnostics rather than public evidence.

The time budget is checked between commands. An in-flight control request can
add the selected supplicant client's send/reply timeout (up to 15 seconds for
the current client). Search windows limit starting further lookups; a lookup
already in flight can supply a candidate within the overall budget.
Resolver retries are reduced for the DNS child process,
and the gateway ping has a three-second deadline. A boot harness should allow
for these in-flight operations and emit its final completion marker for every
result, including SKIP and FAIL.

This checks a requested reassociation and basic connectivity. AP-initiated BTM,
802.11k/r behavior, protected-frame rejection, throughput, and long-duration
reliability have separate acceptance scenarios.
