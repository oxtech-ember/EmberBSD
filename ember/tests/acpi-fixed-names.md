# Fixed-width ACPI name records

ACPI reserved names are four-byte name segments. The predefined-name,
simple-repair and complex-repair tables compare four bytes or print with a
four-character precision. These fields are not C strings.

`ACPI_NONSTRING` marks only the `Name` members of `ACPI_NAME_INFO`,
`ACPI_SIMPLE_REPAIR_INFO` and `ACPI_REPAIR_INFO`. The GCC platform header uses
`__has_attribute(__nonstring__)`. The common type header supplies an empty
fallback for compilers without the attribute. No warning category is disabled.
The table initializers and record layouts remain unchanged.

Run the actual record-type fixture on a 64-bit host:

```sh
HOST_GCC16=/absolute/native/gcc-16 HOST_CC=cc \
  ruby ember/tests/acpi-fixed-names.rb /absolute/source-root
```

On macOS, set `HOST_GCC16_FLAGS="-isysroot $(xcrun --sdk macosx --show-sdk-path)"`
when the selected GCC needs the SDK explicitly. An optional second argument
saves generated C, commands and logs in a new output directory.

The fixture checks the attribute on all three actual fields with GCC16.
It checks four-byte payloads, non-NUL names, adjacent values, callback offsets
and packed/64-bit record layouts. `ACPI_NAME_INFO` stays packed at seven bytes.
The fallback path omits the compiler attribute header and uses the actual
common fallback. Host GCC16 compiles both paths with warnings as errors and
`-fanalyzer`. Native ASan/UBSan runs both paths. Default temporary files are
removed. These checks do not establish full kernel, firmware or device acceptance.
