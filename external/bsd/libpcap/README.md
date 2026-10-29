# Native libpcap

The OS library, pcap-config and rpcapd consume the prepared common Ports
`net/libpcap` stable 1.10.7 source selected by `EMBER_LIBPCAP_SOURCE`.
The OS retains its native BPF headers and installation paths.

The former 1.10.5 and testing 1.11.0 native libraries exported different
private interfaces. The stable library uses SONAME 12; consumers of SONAME
10 or 11 must be rebuilt. The upstream public remote entry path remains
available; the old unreferenced private `rpcap_create` backend is not kept.
DLT 304 remains a reserved numeric identifier outside the stable matching range.
