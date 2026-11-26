# DesignWare EQOS interrupt moderation and IPv4 checksums

The driver defaults to one interrupt request per receive descriptor
and a transmit interval of one. The hw.eqosN.rx_riwt node accepts
0 through 255. A nonzero value sets the receive watchdog count in
units of 256 MAC system clocks. Periodic receive interrupt requests
continue. Returning to zero resumes posting ordinary RX descriptors.
Existing moderated descriptors retain the watchdog until retirement.
A different nonzero count during this drain returns EBUSY.

The hw.eqosN.tx_ioc_interval node controls the transmit interrupt
request interval within a batch. Every nonempty published batch
requests an interrupt on its final descriptor.

When advertised by the MAC, ifconfig can select ip4csum-rx,
ip4csum-tx, tcp4csum-rx, tcp4csum-tx and udp4csum-rx. Generic
defaults leave these flags disabled. The udp4csum-tx flag requires
the separate diagnostic permission hw.eqosN.udp_tx_canary=1.
Returning permission to zero makes new UDP requests use software
when the interface flag remains enabled. IPv6 and TSO stay separate.

The hw.eqosN.hw_feature0 through hw.eqosN.hw_feature3 nodes return
feature words sampled at attachment. The hw.eqosN.dma_32bit_tag node
reports whether the driver installed a lower-4-GiB DMA tag.
These nodes are read-only.

Run the host contracts from the source root:

    python3 ember/tools/eqos-rx-riwt-contract.py
    python3 ember/tools/eqos-tx-ioc-contract.py
    python3 ember/tools/eqos-ipv4-checksum-contract.py

The fixtures use mock descriptors. Verify timer expiry, interrupt
placement and checksum bytes on a directly connected peer before
selecting a board startup profile.
