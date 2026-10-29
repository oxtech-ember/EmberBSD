# Native PPP

The native programs use the complete prepared `net/pppd` source selected by
`EMBER_PPP_SOURCE`. The common Ports recipe carries the NetBSD backend; the
native build keeps the OS integration and links the shared base TDB library.

The direct minconn and passwordfd plugin targets also use this source; they are
not included in the native base build. The legacy status plugin is not part of the
native build; its obsolete LCP hooks require a separate port. The `pppdump`
manual describes the supported output modes and does not promise the former
nonfunctional decompression option.
