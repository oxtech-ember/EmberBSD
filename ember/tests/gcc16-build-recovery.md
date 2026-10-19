# GCC16 build rules and test inputs

Objective-C dependency generation uses the selected `OBJC` compiler when
`USETOOLS=yes`. Native rules default `MKDEPOBJC` to `MKDEP`.
This permits matching GCC16 C and Objective-C frontends from separate prefixes.
C dependency generation remains with `CC`.

ARMv7 hard-float AES and ChaCha tests explicitly select NEON for their NEON
sources. Soft-float ARMv7 keeps its existing ABI flags; AArch64 and x86 do not
receive `-mfpu=neon`.

Run the actual make-rule and cross-object regression with an audited EmberBSD
ARM sysroot and native tools. The negative control requires a GCC16 C compiler
without `cc1obj`, and a separate GCC16 Objective-C frontend for the same target.

```sh
python3 ember/tests/gcc16-arm-build.py \
  --cc /absolute/cross/bin/armv7--netbsdelf-eabihf-gcc \
  --objc /absolute/objc/bin/armv7--netbsdelf-eabihf-gcc \
  --tools /absolute/fork-tools --sysroot /absolute/fork-sysroot \
  --output /absolute/empty-output
```

The check compiles three real crypto translation units. Removing the NEON
option must produce the ISA diagnostic. It also compiles an Objective-C
object and checks the real dependency rule, an incorrect frontend, and C
provider isolation. The output records commands, results and diagnostics.
These are object and dependency checks, not a linked base system or target
execution. Pin the sysroot and compiler independently of the tested source.

The iconv HZ8 regression supplies exactly four bytes, including no added NUL.
The regex harness passes separate escape and continuation backslashes to
`fparseln`. Explicit byte initializers preserve those inputs with GCC16.

```sh
HOST_CC=gcc-16 python3 ember/tests/gcc16-test-inputs.py
HOST_CC=gcc-16 HOST_CFLAGS='-O1 -g -fsanitize=address,undefined' \
  python3 ember/tests/gcc16-test-inputs.py
```

On macOS, add `-isysroot` and the path returned by `xcrun --show-sdk-path`
to `HOST_CFLAGS`. Use `--output` to retain the generated test and compiler
inputs for analysis. `--source` selects another source snapshot for a
before/after comparison. Both checks fail with the changed inputs and pass
with the four-byte input and continuation delimiter restored.

The host test links the complete fork `fparseln` source with a line-input
adapter. It checks joined lines, escaped backslashes and literal comment
characters. It checks the exact iconv input bytes without substituting the
host iconv implementation for EmberBSD. The full native iconv and regex
ATF suites still require an EmberBSD runtime.
