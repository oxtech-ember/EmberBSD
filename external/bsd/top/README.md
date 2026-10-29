# Native top

The OS producer consumes the prepared common `sysutils/top` Ports source via
`EMBER_TOP_SOURCE`. It uses the NetBSD process, CPU and memory backend and
retains the native bmake installation and manual page. The source preserves
the upstream 3.8beta1 archive and the complete maintained NetBSD and pkgsrc
platform changes; the historical `dist` tree is inactive after selection.
