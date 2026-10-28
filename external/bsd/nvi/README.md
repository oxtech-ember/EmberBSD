# Native nvi source

The native editor uses the prepared common `editors/nvi` Ports source selected
by `EMBER_NVI_SOURCE`. Prepare and verify it with the Ports source helper.
The native build retains its DB1 API, wide-character and private regex support,
manual, catalog and installation paths. The separate Ports package uses DB18.
Generated headers and catalogs belong to the object directory.

The retained LICENSE comes from the former native nvi distribution.
