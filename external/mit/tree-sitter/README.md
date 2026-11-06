# Base TreeSitter build

`lib` builds a private position-independent archive from the prepared common
Ports `textproc/tree-sitter` source selected by `EMBER_TREE_SITTER_SOURCE`.
Base gettext uses this runtime with its own pinned language grammars.
No public library, headers, or CLI are installed.

The producer writes `tree-sitter-uninstalled.pc` for the selected host
pkg-config. Its paths select target objects and prepared headers directly.
The gettext consumer restricts pkg-config to target metadata directories.
