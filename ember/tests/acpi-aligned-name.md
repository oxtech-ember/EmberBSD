# Aligned ACPI namespace name

`ACPI_ALIGNED_STR_UNION` stores a string in a local compound union and returns
the aligned typed view at offset zero. Its character member includes one extra
byte for the string terminator. `_REG` still supplies the same four bytes and
`ACPI_NAME` value to namespace lookup. Alignment and block lifetime are unchanged.
The local union does not define a public record layout.

Run the actual macro and `_REG` name fixture on a 64-bit host:

```sh
HOST_GCC16=/absolute/native/gcc-16 HOST_CC=cc \
  ruby ember/tests/acpi-aligned-name.rb /absolute/source-root
```

On macOS, set `HOST_GCC16_FLAGS="-isysroot $(xcrun --sdk macosx --show-sdk-path)"`
when the selected GCC needs the SDK explicitly. An optional second argument
saves generated C, commands and logs in a new output directory.

The fixture checks four-byte data, typed lookup value, alignment and storage
for the terminator. Host GCC16 uses warnings as errors and `-fanalyzer`.
A separate native Clang-compatible build runs under ASan/UBSan. The default
temporary directory is removed. Host checks and an individual target translation
unit do not establish full kernel or ACPI hardware acceptance.
