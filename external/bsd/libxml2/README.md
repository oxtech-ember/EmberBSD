# Base libxml2 build

`gnu` builds a position-independent static library from the prepared common
Ports `textproc/libxml2` source selected by `EMBER_LIBXML2_SOURCE`. It supplies
the native gettext tools and does not install a public library or utilities.
The selected target sysroot supplies zlib; configure obtains its dependency
information through the selected host pkg-config implementation.
