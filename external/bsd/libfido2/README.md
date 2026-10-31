# libfido2 in the base system

Set `EMBER_LIBFIDO2_SOURCE` to the prepared `security/libfido2` source from
EmberBSD Ports for a userland build. The OS retains native make integration,
NetBSD HID support and its base library ABI. It links to base libcbor, OpenSSL,
zlib and usbhid.

The 1.17 integration adds upstream exports while retaining the previous exported
names; the base library changes from `libfido2.so.5.1` to `libfido2.so.5.2`.
Build consumers against the selected common libcbor and OpenSSL sources.
