# Package replacement dry-run regression

`pkg_add -n -u` must not fail only because it skips `pkg_delete`.
The old `start_replacing` initialized its result to `-1` and changed it only
when `Fake` was false. Every simulated replacement therefore failed at that
step. The corrected default is zero. Real deletion still returns its actual
status. Metadata errors still stop replacement.

Run this host check from the source root:

```sh
ruby ember/tools/pkg-add-dryrun-contract.rb \
    external/bsd/pkg_install/dist/add/perform.c /absolute/new-output
```

The runner extracts `preserve_meta_data_file` and `start_replacing` from the
actual source. It retains the source license and line locations. The fixture
uses real temporary files and `rename`. Its explicit process adapter records
the deletion command and returns a selected result. It never executes
`pkg_delete` or changes a package registry.

The 37 cases cover dry-run, verbose output, destination roots, supplied
installed information, missing metadata and all three metadata failure
points. Ordinary deletion returns `0`, `-1` and `7` unchanged. The original
source fails eight dry-run cases. The corrected source passes all cases.

`HOST_CC` and `HOST_CFLAGS` select the host compiler and optional sanitizers.
`EXTRACT_ONLY=1` prepares the two source functions and fixture for the common
GCC16 cross compiler or a static analyzer. Do not use a target binary as a
host executable.

GCC16 compilation and path analysis, Clang Static Analyzer and Cppcheck pass
for the extracted functions and fixture. Host ASan and UBSan report no memory
or undefined-behavior errors. These checks do not compile the complete
`pkg_add` translation unit or test a complete package update. Valgrind is
unavailable on the Darwin AArch64 test host.

Later dependency and required-file checks remain active. This change does
not implement a complete virtual registry for simulated dependency upgrades.
It also does not change an already installed `pkg_add` binary.
