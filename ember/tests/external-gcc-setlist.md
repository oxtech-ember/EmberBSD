# External GCC ownership in AArch64 sets

The external GCC adapter selects build-only headers, CRT and libraries.
It does not install them into the OS destination. `TOOLCHAIN_MISSING=yes`
disables the in-tree GCC producer; it must not make the base set own a
compiler SDK, duplicate runtime libraries, or `/usr/pkg/gcc16` package files.
A declared Ports runtime provider owns the target DSOs separately.

Run the host regression with native tools and a new output directory:

```sh
ruby ember/tests/external-gcc-setlist.rb /absolute/os-source \
  /absolute/native-tooldir /absolute/new-results
```

The fixture uses the complete actual `base/ad.aarch64`, selected ordinary
records from `base`, `comp` and `etc`, and unchanged `mkvars.mk`, `sets.subr`,
`makeflist` and `checkflist`. Real BSD make evaluates the producer options.
Both external and in-tree configurations retain ordinary OS files and startup
objects. The in-tree control retains its selected GCC runtime. Seven deliberate
missing/extra-file cases must fail the strict checker. These include SDK headers,
package runtime files and a duplicate external base runtime.

The former AArch64 addition makes the external case fail with 926 missing paths.
Removing that unrelated payload restores the fixture without adding files or
relaxing diagnostics. Each case retains logs and a JSON selection receipt.
The inherited checker compares destination inventory and selected lists;
its metadata comparison is disabled. This test does not claim a `METALOG` audit.

This is a set ownership contract. It does not compile target code, generate
complete base/etc archives, package a runtime, check ELF dependency closure,
or establish target execution. Audit the full destination, installation metadata
and one selected runtime provider before composing or installing a release.
See the [base update guide](../tools/docs/aarch64-base-update.md) and
[external adapter guide](../../tools/docs/external-gcc-runtime.md).
