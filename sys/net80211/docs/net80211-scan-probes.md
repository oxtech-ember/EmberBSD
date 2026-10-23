<!-- Origin: EmberBSD optional scan probe policy contract, AI-assisted. -->
# Optional scan probe policy

`ic_scan_probe_admit` is an optional synchronous policy callback under Giant.
It returns zero to permit a probe request or an errno to reject it.
The callback must not sleep, mutate state, or retain node or channel pointers.
It inspects `ic_curchan`, which is the current scan channel. A BSS node can still
refer to an earlier channel. NULL preserves the existing probe behavior.

`ieee80211_send_probereq()` checks the callback before allocating a frame,
retaining a node or changing a queue. It returns the callback's exact error.
Direct callers, active scans, forced probes after passive-channel beacons and
beacon-miss probes use that same entry point. A beacon does not override a
callback rejection. Existing callers can ignore the error without posting a
probe frame.

This callback restricts probe creation only. It does not advertise channels,
authorize authentication or data, establish a regulatory domain, expire a
beacon lease, stop queued frames or prove physical RF silence. A driver must
apply its transmission policy at its own final producer boundary too.

`ember/tools/net80211-wme-contract.sh` runs the actual producer, scan helper and
beacon consumer. Cases cover direct, forced and beacon-triggered refusal,
errno retention, no allocation or node reference change, and optional-policy
compatibility. These are software contracts.
