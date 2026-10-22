# Focused GCC16 userland contracts

The Python standard-library runner extracts the selected functions from the
current sources and compiles them with a real GCC16 host compiler. Its controlled
callbacks, terminal state, command evaluator and I/O peers exercise short writes,
option-list ownership and deletion, script-command input, narrow-terminal
wrapping, object outputs, CPU labels, accounting allocations, progress bars and
fork-isolated command substitutions. These checks do not execute the complete
utilities. The optional local Git baseline must precede the fixes: every baseline
case must reproduce its regression, while every current-source case must pass.

```sh
python3 ember/tests/gcc16-userland-recovery.py --cc /path/to/gcc-16 \
  --baseline PRE_FIX_COMMIT --sanitize /absolute/new-work-directory
```

Set `HOST_GCC16_FLAGS` for host-specific compiler options, such as the macOS SDK
path. The new work directory retains extracted sources with their original
licenses, exact compiler commands, diagnostics and `results.json`. Plain strict
warning checks run before ASan/UBSan because instrumentation changes GCC's
`dobackp` dangling-pointer diagnostic. GCC and Clang static analysis use these
same fixtures; Clang is an analyzer only. Linux Memcheck checks may use a separate
native host compiler, whose version must be recorded independently from GCC16
target compilation.

The complete ARMv7 translation-unit qualification used GCC16 with current source
headers and a pre-existing sysroot based on revision
`7a48aa5f91213866f683e05676c12bcfa9b182ff` plus local changes, recorded by source
fingerprint `f2e3b675c3a4d3f6a91e59a0a6e5fc4d82e1698a30171e2a7072d5c9cf4f1926`.
Three generated headers were copied with recorded hashes after checking their
producer sources against current main. This establishes object compilation, not
a fresh userland image, linking or target execution. Full-file GCC analysis
still reports the baseline finding in mail's `smopts_core` allocation and
initialization model. That finding is outside the selected repairs.
`tipout` no longer calls `exit` from a signal handler. `ember/tests/tip-stop.md`
checks that cleanup. Cppcheck also retains inherited host header/style and
controlled-peer findings, so the checks do not claim that the entire utilities
are analyzer-clean.
