# One-shot station Action TX diagnostic

`wifi-sta-action-once` sends a caller-supplied 802.11 Action body through the
versioned common net80211 STA management request. It requires root and a current
associated STA on an adapter that advertises the corresponding STA TX support.
The tool sends one frame per invocation and does not choose or construct a
protocol payload. The ESP external-WPA producer includes the transport without
an additional diagnostic build option.

Usage:

```sh
wifi-sta-action-once interface action-body-hex
```

The hexadecimal argument contains the Action body beginning with category and
action code. The kernel validates supported BTM/RRM body syntax and the current
association generation, channel, BSSID and controlled-port state. The adapter
accepts another request after the preceding transport operation has drained.
An in-flight operation keeps its firmware resources even after logical
cancellation.

A zero exit status means a matching local TX completion reported success; a
nonzero status means the adapter reported a local send failure. ESP completes
after the correlated firmware callback drains or submission proves that no
buffer was accepted. Its callback has no ACK result, so the output says that
peer ACK is unconfirmed. Adapters that can report ACK status distinguish a
confirmed ACK from a known no-ACK result. A peer response is needed to
establish a successful RRM/BTM exchange.
