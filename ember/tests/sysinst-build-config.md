# Incremental sysinst identity

The installer compiles `EMBER_VERSION`, `EMBER_BUILD_ID` and the selected
base runtime mode into its objects. It also substitutes the identity into
generated messages. Compiler flags alone do not invalidate cached objects.
An incremental release could therefore carry a new BUILDID with an old
installer. The guided preflight correctly rejects that mismatch before writes.

`ember-build-config` records these effective inputs in each sysinst object
directory. Make compares its contents on each invocation. A changed stamp
invalidates all program objects and `msgtouch`, which rebuilds the ordinary
message, menu and catalog outputs. Unchanged contents preserve the stamp's
timestamp. The normal clean rule removes the stamp and temporary file.
The runtime entry records gcc16 versus base, not a private SDK path.

Run the real amd64 Makefile, compiler and message generators in a new tree:

```sh
ruby ember/tests/sysinst-build-config.rb /absolute/source \
    /absolute/configured/nbmake-amd64 /absolute/audited/gcc16 /new/output
```

The runner copies the configured wrapper and redirects its source and object
paths. It reads the prepared destination headers; it does not install files.
It compiles actual `wizard.o` and `main.o` and generates all four translated
catalogs. It checks identity changes, adoption of old objects without a
stamp, version changes, both runtime modes, unchanged repeats and switch-back.
Independent invocations span a BSD make timestamp tick. It also checks the
compiled build ID and runtime branch, message identity and reproducible bytes.

The original Makefile leaves `wizard.o` stale after the build ID changes.
This is a cross-build dependency regression. A normal distribution/release
rebuild and new VM installation remain required. The BUILDID comparison,
runtime bundle validation and preflight write barriers are unchanged.
