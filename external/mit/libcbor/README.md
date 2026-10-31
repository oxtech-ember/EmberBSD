# libcbor in the base system

Set `EMBER_LIBCBOR_SOURCE` to the prepared `devel/libcbor` source from
EmberBSD Ports for a userland build. The OS makefiles retain native build and
installation and generate the configuration and export headers in the object
directory. The current 0.14 integration uses the upstream default buffer growth,
stack limit and pretty printer settings.

Base `libcbor.so.1` replaces `libcbor.so.0`: 0.14 removes an exported 0.5
symbol. Rebuild dependent consumers against the new headers and library.
