# Native tcpdump

The native producer uses `EMBER_TCPDUMP_SOURCE` for the complete tcpdump
source prepared by the shared Ports owner. Its native configuration, PF
support, installation paths and manual section remain part of the OS build.
The program links to the native libpcap and crypto libraries.

The 4.99.7 source no longer includes OTV decoding: upstream removed it in
4.99.6 after the associated Internet-Draft became inactive.
