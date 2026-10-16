# libc stack canary initialization

`__guard_setup` changes `__stack_chk_guard`. With strong stack protection,
its address-taken `size_t len` also causes GCC16 to protect this function's
frame. The prologue saves the initial zero canary; the epilogue compares it
with the newly initialized canary and calls `__stack_chk_fail`. This happens
both after successful entropy acquisition and after the fallback canary is
installed. A preinitialized guard does not reproduce the failure.

The initializer alone uses `no_stack_protector` when the compiler advertises
that attribute. The rest of libc, including the failure routine, retains
its existing compiler flags. There is no global or file-wide SSP exclusion.
The attribute is supported by the current GCC16 and Clang21 providers; the
availability test preserves compilation with older compilers.

Run the extracted, unchanged initializer with controlled entropy outcomes:

```sh
ruby ember/tests/libc-guard.rb /absolute/source /absolute/host/gcc /new/output
LIBC_GUARD_ASAN=yes ruby ember/tests/libc-guard.rb /absolute/source /usr/bin/clang /new/asan-output
```

The harness explicitly selects the global canary ABI. It tests successful
entropy, failed syscall, short result, an existing guard, repeated calls,
and a deliberately corrupted separate protected frame. Removing the
initializer attribute makes the first three cases exit through the failure
hook. The hook uses exit99 to distinguish this failure from host signals.
The entropy syscall and failure reporting are controlled test boundaries;
this is not execution of target libc on the host.

Replay the complete target translation unit using the existing configured
libc make flags and generated headers, with all outputs in a new directory:

```sh
CPPCHECK=/absolute/cppcheck ruby ember/tests/libc-guard-target.rb \
    /absolute/configured/nbmake-amd64 /absolute/candidate/source /new/target-output
```

The configured source is the baseline and must differ from the candidate.
The runner queries the real `.o`, `.pico` and `.po` recipes, preserves strong
SSP and `-Werror`, and checks their disassembly. Only the candidate setup
function loses its canary check; another protected frame remains. It also
runs GCC's path analyzer and Cppcheck on the real preprocessed baseline and
candidate. Cppcheck style findings are retained separately, without
suppression. The original source, object tree and destination are read-only.

For the observed amd64 image, both dynamic `/sbin/init` and static ramdisk
startup reach `_libc_init` and its guard initializer. The libc failure
routine attempts `SIGABRT` before its exit127 fallback; PID1 has special
signal handling. The source fixture and target disassembly establish the
false failure; they do not by themselves establish that a rebuilt VM boots.

Integration must rebuild libc's shared, static and profiling variants,
relink static consumers (including rescue and installer `ramdiskbin`),
regenerate the INSTALL kernel with its new ramdisk, and rebuild distribution
and release images. Keep the audited GCC runtime package in the image's
bootstrap closure. Execute the resulting image in the owned VM before
claiming boot acceptance. No target development tools are needed.
