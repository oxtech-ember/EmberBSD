# Native libpcap

The OS library, pcap-config and rpcapd consume the prepared common Ports
`net/libpcap` source selected by `EMBER_LIBPCAP_SOURCE`. The OS retains its
native build integration, BPF headers and installation paths.

The move from 1.10.5 to 1.11.0 changes previously exported internal interfaces.
The native library therefore uses SONAME 11; binary consumers linked to SONAME
10 must be rebuilt. This adds DLT constants from upstream without claiming new
hardware support.
