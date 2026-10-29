# Native NetBSD iSCSI

The OS library, initiator and target use the prepared common
`devel/netbsd-iscsi-lib` Ports source selected by `EMBER_ISCSI_SOURCE`.
The source combines NetBSD's maintained 20111006-based implementation with
the shared pkgsrc portability changes. The OS retains its native makefiles,
headers, shared library ABI, manuals and installation paths. The historical
`dist` tree is inactive after selection.
