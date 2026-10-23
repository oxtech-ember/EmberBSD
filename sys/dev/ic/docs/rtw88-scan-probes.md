<!-- Origin: EmberBSD native RTL8822CE scan probe policy, AI-assisted. -->
# RTL8822CE scan probe restriction

Native attach registers the shared optional scan probe policy in every profile.
The policy uses the current scan channel, independently of the BSS node's older
channel. It rejects unset channels, invalid numbers and channels 52 through 144.
A received
beacon cannot enable active probes on those DFS channels.

The policy has no device I/O or state mutation. Direct requests, forced scan
requests and beacon-triggered requests use the shared check before allocation.
`rtw88-enable-contract.sh` runs those actual paths and checks distinct BSS and
scan channels, no new DMA submissions and unchanged node references.

This restriction does not advertise DFS channels or authorize authentication,
data or VHT. The existing channel table still excludes channel 64. Radio silence
and DFS operation require separate hardware acceptance.
