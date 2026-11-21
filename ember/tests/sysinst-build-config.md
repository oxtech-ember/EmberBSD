# Incremental sysinst identity

The installer embeds the release identity and the selected base runtime mode.
The guided preflight compares the build ID with the installation media before
writing. A retained object directory must track these inputs even when no C
source changed.

The sysinst Makefile records the build ID, version, runtime mode and download-directory selection in
separate files in the object directory. Each stamp keeps its timestamp while
its contents match. A build ID or version change rebuilds the wizard and
regenerates its messages, menus and translated catalogs. The build ID also
updates `main.o` on platforms whose default download directory includes that ID.
The accepted amd64 URL and an explicit `SYSINST_FTP_DIR` are independent of the ID.
Changing or clearing that override updates `main.o` and `wizard.o`.
The runtime mode (base or gcc16) affects `wizard.o` and `util.o`. The normal
build produces translated catalogs for installation as data files.

Run the check with the configured cross-build wrapper and prepared SDK paths:

```sh
ruby ember/tests/sysinst-build-config.rb "$OS_SOURCE" "$NBMAKE" \
    "$GCC_RUNTIME" "$NEW_OUTPUT"
```

Pass `-` for the runtime argument with a Clang wrapper. A GCC wrapper takes
its audited external runtime and also exercises switching to and from that mode.
The runner redirects the wrapper to a new object directory and reads the
prepared destination headers. It compiles `wizard.o`, `main.o`, `util.o` and
`txtwalk.o` and generates all four translated catalogs. It exercises a retained
directory missing an identity stamp, independent changes to each input, supported by the selected compiler,
and unchanged repeats. Returning to the original settings must reproduce the bytes
of every tested output. The runtime input selects the installer's bundle mode.
This test exercises the build dependency and does not install a VM.
