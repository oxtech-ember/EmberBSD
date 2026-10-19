# GCC16 firmware profile

`../build-firmware.sh` builds the pinned RPi5 TF-A and EDK2 sources with
native host tools and the common AArch64 GCC major 16 toolchain.
Set `CROSS_COMPILE` to the compiler prefix. Clang is not a target compiler.
The compiler triplet and architecture macros remain unchanged.

The host needs Git, Ruby and patch for source preparation, GNU Make, iASL, DTC, Python 3,
and a current pyelftools module. These are existing upstream dependencies.
A compiler identity check and the smoke tests below do not establish a
complete firmware build or acceptance on a physical board.

```sh
bash ember/build-firmware.sh /absolute/cache --prepare
CROSS_COMPILE=/absolute/gcc16/bin/aarch64--netbsd- \
    bash ember/build-firmware.sh /absolute/cache --rp1-console
```

The optional console and trace variants keep their platform defines.
Use `--rp1-trace` for the trace variant. A normal build selects the standard
variant. Each variant has its own output and receipt. A rebuild invalidates
its selected old image before checking tools or sources. `--prepare` needs
Git, Ruby and patch and preserves accepted outputs. See the
[verified source-cache contract](source-cache.md) for source manifests,
shared-cache leases, legacy adoption and object-profile limits.

## Header and linker contract

The profile applies only inside the firmware invocation. It does not change
the compiler installation or application builds. `gcc-profile.sh` verifies
that the standard compiler headers belong to the selected compiler's
physical `lib/gcc/MACHINE/VERSION/include` directory. It excludes
`include-fixed` and OS headers. The compiler uses `-ffreestanding`,
`-nostdinc`, and an explicit compiler header directory. The firmware-only
`-U__NetBSD__` prevents GCC's `stddef.h` from requesting `machine/ansi.h`.
Compiler standard and intrinsic headers remain available.

A private empty directory serves as the firmware search root. It must not
be a symlink. The builder removes it on exit and fails if it becomes
nonempty. The linker uses `-nostdlib`, `-static`, and actual ld `-nostdlib`.
`gcc.specs` removes the driver's implicit runtime library directories under
`-nostdlib`. Explicit firmware `-L` paths remain valid, including EDK2's
`GccLto` archive. This preserves EDK2 module flags and LTO.

The pinned top-level API splits TF-A and EDK2 flag strings on whitespace.
CACHE, source and explicit toolchain paths must not contain whitespace.
Invalid explicit inputs preserve accepted outputs. A compiler path found
by tool lookup is checked after the selected rebuild is invalidated.

The bounded invocation clears inherited compiler header/library search
variables, ordinary compiler and linker flags, tool overrides, and GNU Make
overrides. Host tools use their native defaults. The selected
`CROSS_COMPILE` and the upstream platform/module options remain active.
TF-A receives explicit `CC` and `LD` tokens for the validated GCC driver,
plus separate `CFLAGS+=` and `LDFLAGS+=` tokens. The EDK2 DSC patch
uses single `=` assignments to append the profile. It does not use `==`.

The receipt records the compiler identity and SHA256, profile and specs
hashes, and the injected TF-A/EDK2 flags. The builder checks the compiler
hash again before publication. It hashes the incoming image, writes the
receipt, then publishes the final image. A late failure removes the image
and receipt and retains the build log.

## Bounded source and compiler controls

The actual-script host controls use fake services, including the source-helper
boundary. The separate source controls use the actual helper and real Git.
The builder controls need Ruby and a
host Python 3 for the existing receipt writer. They perform no network
requests, source resets, real firmware builds, or device access.

```sh
ruby ember/tools/firmware-contract.rb
```

The real compiler controls need GCC16 and its target binutils, GNU Make,
Ruby, `patch`, and immutable pinned firmware source inputs. Use the verified Git
checkout produced by `--prepare`. The smoke reads the original DSC from its
pinned Git blob, even when the working series is already applied.
The top-level pin is `246e70acd939d62b5a79f895984afd0a93bd83e9`.
Its TF-A pin is `a5c580fd539929af41cf483b7c1434db261dd51b`.
Its EDK2 pin is `7bbf1f2022f381f48cfb5411df9e5157ee0291a5`.
Its edk2-platforms pin is `c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed`.
Use an empty output directory:

```sh
FIRMWARE_TEST_GMAKE=/absolute/host-tools/bin/gmake \
    ruby ember/tools/firmware-gcc-smoke.rb \
    /absolute/gcc16/bin/aarch64--netbsd-gcc \
    /absolute/cache/rpi5-uefi-src /absolute/smoke-output
```

The smoke applies only RPi5 DSC hunks from the ordered series to an output
copy. It checks compiler headers, two translation units, real LTO objects,
actual static ELF links, maps, and library searches. It retains explicit
firmware archives. It records command arguments, output, dependencies,
input hashes and ELF metadata. The pinned TF-A make fragment verifies flag
accumulation and isolation from inherited flags. Its actual toolchain helper
verifies the selected GCC compiler and linker driver. This is not a full series,
TF-A, EDK2, or physical-board acceptance test.

The minimal baseline ELF can omit `PT_INTERP`. A driver `-###` projection
alone does not establish ELF metadata. The separate injected
`--no-as-needed -lgcc_s` control demonstrates an unintended runtime input;
it does not claim that upstream normally requests this library. A static
archive lookup control independently tests implicit search isolation.

The ordered series dispatches `tfa-*.diff` files to `arm-trusted-firmware`
and `edk2-*.diff` files to `edk2`. Other diffs target `edk2-platforms`. The TF-A shell-slice patch terminates the
`sed` print command before its closing group brace. It uses stock macOS
BSD `sed` and does not require a replacement `sed`.

The host regression executes the actual pinned `utilities.mk` through GNU
Make. It preserves a failing upstream control and checks slice boundaries,
spaces, quotes, and an empty argument. Export the original pinned helper
without changing the prepared working file:

```sh
git -C /absolute/cache/rpi5-uefi-src/arm-trusted-firmware show \
    a5c580fd539929af41cf483b7c1434db261dd51b:make_helpers/utilities.mk \
    > /absolute/reference/utilities.mk
FIRMWARE_TEST_GMAKE=/absolute/host-tools/bin/gmake \
    ruby ember/tools/firmware-tfa-host.rb \
    /absolute/reference/utilities.mk /absolute/tfa-host-output
```

This is a host-helper check. It does not establish a full firmware build.

The EDK2 shell-wrapper patch uses a nonnegative last `BASH_SOURCE` index.
It preserves the outermost sourced script and every argument on stock
macOS Bash 3.2 and current Bash. It changes only host-tool dispatch.
The regression exports the two actual pinned wrappers from Git. It tests
direct execution, sourcing from `bash -c`, and nested script sourcing with
an explicit argument-capture backend. Use explicit shell paths:

```sh
ruby ember/tools/firmware-edk2-wrapper.rb \
    /absolute/cache/rpi5-uefi-src/edk2 /absolute/wrapper-output \
    /bin/bash /absolute/current-bash/bin/bash
```

The upstream Bash 3.2 control emits `bad array subscript`. Its `bash -c`
sourcing case can return zero without executing the backend. The patched
wrapper must execute the correct backend without a diagnostic. This test
does not compile firmware or establish acceptance on a physical board.

## Complete series source check

Use a prepared canonical firmware Git checkout as SOURCE. The checker reads
only pinned Git blobs, even if a failed build changed its working files.
It exports only referenced source paths into EMPTY_OUTPUT. It verifies the
top-level and patched-submodule pins, rejects unsafe paths, and applies the
whole ordered series forward with `-t -N -p1 -F0`. It performs no fetch,
checkout, reset, compiler invocation, or changes to SOURCE.

```sh
ruby ember/tools/firmware-series.rb \
    /absolute/cache/rpi5-uefi-src /absolute/series-output
```

The receipt records input, patch and final source hashes. C/ASL source
assertions preserve the 125/255 enabled-PWM handoff, the existing FRQS mask
and cooling mapping, and unavailable RPM reporting. A repeated-patch control
must fail without a prompt or reverse application. These checks do not
establish C/ASL compilation, a complete build, or physical acceptance.
