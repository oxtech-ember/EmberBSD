# ACPI GPIO resource contract

Run the actual resource lookup functions against a host double for
`AcpiWalkResources`:

```sh
git show "$BASELINE_COMMIT:sys/dev/acpi/acpi_gpio.c" > /tmp/acpi_gpio.before.c
HOST_CC=/absolute/path/to/gcc-16 python3 sys/dev/acpi/tests/acpi-gpio-resource.py \
    --source /tmp/acpi_gpio.before.c --expect-uaf
HOST_CC=/absolute/path/to/gcc-16 python3 sys/dev/acpi/tests/acpi-gpio-resource.py
HOST_CC=/absolute/path/to/gcc-16 python3 sys/dev/acpi/tests/acpi-gpio-resource.py \
    --no-sanitize
HOST_CC=/absolute/path/to/gcc-16 python3 sys/dev/acpi/tests/acpi-gpio-resource.py \
    --source /tmp/acpi_gpio.before.c --no-sanitize --expect-escape
```

Set `BASELINE_COMMIT` to a revision before this fix.

The sanitizer run frees the `_CRS` buffer after the callback, as ACPICA does.
AddressSanitizer must report a heap use-after-free for the pre-fix source.
The fixed source must pass with ASan/UBSan. The `--no-sanitize` run retains the
buffer until process exit and uses a callback guard. It proves that the lookup
does not access a resource outside the callback, without claiming sanitizer
coverage. Use `--no-sanitize --expect-escape` with the pre-fix source when the
selected GCC has no usable ASan runtime.

For a cross compiler, use compile-only mode with a new absolute output
directory. This mode saves the generated fixture, binary, compiler metadata,
hashes, exact compile arguments, and compiler output. It never executes the
binary on the build host:

```sh
HOST_CC=/absolute/path/to/gcc16-cross \
HOST_CFLAGS='--sysroot=/absolute/target-sysroot' \
python3 sys/dev/acpi/tests/acpi-gpio-resource.py --compile-only --no-sanitize \
    --output-dir /absolute/new/acpi-gpio-resource
```

The receipt is `build.json`. It records the source and generated fixture
hashes, compiler invocation and resolved path, resolved GCC file hash, GCC
version and triplet, exact command, compile exit status, and
`execution: not-run`. Run `acpi-gpio-resource` separately on the matching
target. A successful cross compile does not establish that target execution
or the kernel fix passes.
This receipt does not attest to the sysroot or its ABI provenance. Keep the
matching accepted sysroot receipt with this build record.

`--expect-escape` is valid only with `--no-sanitize`. It is an executable
baseline guard, so compile-only mode rejects it.

The fixture checks interrupt and I/O selection, pin-table and source-string
access during the callback, interrupt mode mapping, missing resources, and
propagated lookup failures. It checks all five interrupt modes. For macOS GCC,
set `HOST_CFLAGS` to include the selected SDK with `-isysroot`.

This host contract does not execute ACPI firmware, attach a GPIO controller,
or establish physical-device acceptance. Build the affected kernel and test
the device separately after review.
