# Station association lifecycle identity

`ieee80211com.ic_assoc_epoch` is a monotonic, per-interface identity for a
selected station BSS. `ieee80211_sta_join()` assigns a nonzero epoch before it
replaces the current BSS. The value survives ordinary INIT/scan transitions;
the same BSSID selected again receives a different epoch. Exhaustion rejects
the selection before changing `ic_bss`.

The epoch identifies common net80211 selection, not successful authentication,
key installation, or authorization. The MLME request uses
`IEEE80211_MLME_VERSION`: callers provide that version and zero
`im_assoc_epoch`; a successful STA ASSOC copies the accepted epoch and selected
BSSID back into the same request. The kernel MLME implementation retains a node
reference across the synchronous driver state callback and the result copyout.
If copyout fails, net80211 cancels only the selection that is still current; it
never restores a previous BSS. Kernel and userland consumers must be upgraded
together: old MLME request sizes and requests without the v1 version are
rejected, and the route consumer must understand the v1 association payload.

STA BSS `RTM_IEEE80211_ASSOC`, `REASSOC`, and `DISASSOC` carry the same
`ieee80211_assoc_event` v1 payload: version, zeroed reserved fields, epoch, and
BSSID. The leave path snapshots role and identity before node cleanup. A late
old-epoch leave may still be reported to the consumer, but it cannot lower the
link state of a replacement association. AP station JOIN/REJOIN/LEAVE and
non-STA BSS notifications keep their existing event format.

The 64-bit counter and node stamp are accessed under the common per-interface
IPL_NET lock. This avoids relying on 64-bit atomic operations on RV32. The lock
covers only epoch metadata; it does not serialize `ic_bss` selection or node
ownership. Those remain under the existing network/Giant serialization. The
ESP and AICWF interfaces do not set `IFEF_MPSAFE`, so their ioctl callbacks use
the existing Giant-protected path. Copyout, callbacks, node cleanup, and route
message allocation remain outside the epoch lock.

RUN-to-AUTH and RUN-to-ASSOC retry paths snapshot and retire the old identity,
then stamp a new epoch on the retained BSS node before sending the retry
request. Adapter generations and SAE lifetime cookies remain separate from
this common association identity.
