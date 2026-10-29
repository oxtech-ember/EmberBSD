# Native gettext tools

The base tools use the prepared `devel/gettext` source selected by
`EMBER_GETTEXT_SOURCE` and the shared `textproc/libxml2` source selected by
`EMBER_LIBXML2_SOURCE`. The operating system retains its native program,
manual, Info and locale installation rules. The historical source tree is no
longer part of this build graph.

The base profile uses the common plain-output mode: ordinary PO and XML
processing and message translations are retained, while the newer `--color`
and `--style` options report that styling is unavailable. Base `libintl`
remains the BSD Citrus implementation. The common libxml2 producer builds a
private static position-independent archive for the tools; it does not install
another libxml2 or libintl into the base.
