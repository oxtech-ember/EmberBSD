# NVMM machine-creation ownership

Machine creation allocates its area-list head and VCPU page-pointer array in userspace.
The create ioctl carries only a machine ID, not either allocation pointer.
The library publishes the allocations through `mach` only after the ioctl succeeds.
A failed ioctl therefore leaves both allocations owned by the current function.
The failure path now frees `pages` as well as `areas`.
Success and public ABI remain unchanged.

## Run the regression

```sh
ruby ember/tests/nvmm-ownership.rb
```

The runner extracts the actual create/removal functions, area types and public machine descriptor.
It copies the unchanged portable OS `queue.h` into its automatic temporary directory.
The fixture controls allocation and ioctl results and records ownership.
It checks allocation failure at each allocation, ioctl failure and successful publication.
Failures must retain the original output descriptor and `errno`, and free every owned allocation.
Success transfers both allocations to `mach` and retains zeroed page pointers.
The default run uses Clang ASan/UBSan; GCC16 analysis and Clang Static Analyzer also cover the fixture.
Set `HOST_CC` and `HOST_CFLAGS` to select those analysis modes.
Valgrind is unavailable on the macOS host; sanitizer checks are not Valgrind checks.

The same fixture exercises actual area removal with zero through three entries.
The source list invariant is explicit: the head entry's `le_prev` points to `areas->lh_first`.
`LIST_INSERT_HEAD` establishes it, and `LIST_REMOVE` updates that link to `le_next` before `free`.
The next iteration therefore visits the next live entry.
The test checks each allocation is freed exactly once, including with `QUEUEDEBUG` enabled.
It does not promise safety for externally corrupted list links or repeated destruction of a dead machine.
Three negative controls omit the pages free, publish output before ioctl success or omit list unlinking.
Each control fails its assertion or sanitizer check.

## Evidence boundary

The real target libnvmm command passes with its original diagnostics.
Full-TU GCC16 analysis removes the machine-creation leak report.
Its inherited list-removal double-free report remains visible.
Source invariants and the actual removal fixture identify an alias-model false positive for valid library lists.
No production list rewrite or analyzer suppression is added, and the full analyzer run still exits nonzero.

Cppcheck analyzes actual GCC-preprocessed target TU inputs.
This ownership fix decreases its reports from 127 to 126 by removing the pages leak.
The preceding batch-lifetime fix decreased 128 to 127.
Ten other stack-address reports and the remaining style findings stay with the libnvmm owner.
The target-preprocessed fixture also receives Cppcheck analysis.
It reports one declaration-scope suggestion and the deliberate always-failing allocator assertion used to reject unknown frees.
These test findings remain visible without suppressions.
The checks establish isolated compilation and host ownership contracts.
They do not exercise the NVMM device, a guest VM or hardware virtualization.
