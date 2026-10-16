# Stack protection with the external GCC16 compiler

Target builds that select external GCC use `-fstack-protector-strong`.
They retain `-Wstack-protector`, `-Werror` and `ssp-buffer-size=1`.
In-tree compiler and Clang selection keeps the existing basic SSP policy.
The target choice does not change host program or host library flags.

GCC16 basic SSP diagnoses an addressable local aggregate that contains a
zero-length array, even with the one-byte buffer threshold. Its diagnostic
says that all local arrays are smaller than one byte. A zero-length array
does not become a character buffer when the threshold is one.
Strong SSP protects the addressable aggregate and address-taken scalar locals.
The BCSP and netbsd32 signal translation units reproduce the diagnostic with
the actual GENERIC compiler arguments. They compile with strong SSP and the
same warning policy. The reduced fixture isolates the zero-array case.

This selection leaves the existing SSP exclusions in place:

- `NOSSP`, `USE_SSP=no`, boot programs under `/usr/mdec`, and Coverity setup.
- The `kern_ssp.c`, `cpu.c` and `subr_kcov.c` per-file `-fno-stack-protector`
  overrides in `sys/conf/ssp.mk`.

The external-toolchain workflow separately verifies GCC16 identity and target.
Do not change `HAVE_GCC` to identify an external compiler. That variable selects
the imported in-tree compiler sources. No warning suppression is added here.

## Checks

Run the metadata and assembly regression with real NetBSD make and GCC16:

```sh
CPPCHECK=/absolute/bin/cppcheck \
ruby ember/tests/external-ssp.rb /absolute/tools/bin/nbmake-amd64 \
  /absolute/os-source /absolute/gcc16-prefix
```

The test uses the wrapper's sibling `nbmake` with explicit target settings.
This avoids retaining the wrapper's external compiler in an in-tree fixture.
It checks external GCC, in-tree GCC, external Clang, host flags and exclusions.
It then compiles `fixtures/ssp-gcc16.c` with the selected GCC16 driver.
Assembly must contain both guard accesses and `__stack_chk_fail` for one-byte
and larger character buffers. Strong SSP must also protect the nested
zero-array aggregate and the address-taken scalar. The basic character-buffer
coverage must remain present. A plain scalar needs no guard, and the existing
per-file no-SSP override must still disable instrumentation.
GCC `-fanalyzer` runs on the fixture. Set `CPPCHECK` to run Cppcheck as well.

To reproduce the two real GENERIC translation units, retain the diagnostic
build log and its configured kernel directory:

```sh
SSP_KERNEL_DIR=/absolute/kernel-objects/GENERIC \
CPPCHECK=/absolute/bin/cppcheck \
ruby ember/tests/external-ssp.rb /absolute/tools/bin/nbmake-amd64 \
  /absolute/os-source /absolute/gcc16-prefix \
  /absolute/kernel-diagnostics.log /absolute/new-ssp-results
```

The optional run extracts the actual BCSP and netbsd32 signal compile commands.
It changes only their output paths, then requires the original SSP diagnostics.
It substitutes the strong-SSP flag obtained from the make regression and requires
successful compilation with the original `-Werror`. It saves compiler logs and
the corrected argument arrays. It does not rebuild or boot the whole kernel.
