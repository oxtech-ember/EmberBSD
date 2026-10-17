# External GCC C++ dependency headers

The external GCC runtime adapter selects staged C++ headers with
`-nostdinc++` and three `-isystem` paths. `bsd.dep.mk` filters ordinary
`CXXFLAGS` before dependency generation. That filter discards these options
and their separate path arguments. A source can compile successfully while
its `.cpp` to `.d` step fails to find `<cassert>` or `<cstring>`.

`MKDEPCXXFLAGS` supplies C++ preprocessing options that the filter cannot
preserve. Only the C++ dependency rule uses it. The adapter defines one
`_EMBER_GCC_CXX_CPPFLAGS` list for both `CXXFLAGS` and `MKDEPCXXFLAGS`.
The C preprocessing rules do not receive this list. The existing adapter
guards still exclude `HOSTPROG`, `HOSTLIB` and unselected runtime builds.
The runtime stage remains a build input. This does not install development
headers into the target image or change the target runtime package policy.

Run the regression with the genuine GCC16 application compiler, completed
runtime stage, fork sysroot and native build tools. Use a new receipt path.

```sh
ruby ember/tests/external-cxx-depend.rb \
  /absolute/tools/bin/nbmake "$PWD" \
  /absolute/application-gcc-prefix /absolute/runtime-stage/usr/pkg/gcc16 \
  /absolute/target-sysroot /absolute/new-receipt
```

The test runs the real BSD make compile and dependency rules. It compiles
a C++ probe, creates its dependency file, and compares the provider with
the compiler's `-H` trace. It also checks a C source and host/default flag
isolation. Finally it runs the actual `libatf-c++` Makefile on `check.cpp`
to build `check.o` and `check.d` in a private object directory. It builds no
complete ATF library and runs no target executable.

Set `CXX_DEPEND_BASELINE_GCC` to an original `bsd.gcc.mk` snapshot to compare
unselected/default and host flags exactly. With the original make files,
the C++ probe must compile and its dependency step must fail on `<cassert>`.
For a causal negative control, set `MKDEPCXXFLAGS=` when invoking the same
dependency rule. That dependency build must fail again.

Receipts retain argv, logs, provider metadata and input source hashes.
The existing `external-gcc-runtime.sh` regression covers the architecture,
CRT and host guards separately. These checks do not prove a complete OS
build, VM execution or native desktop acceptance.
