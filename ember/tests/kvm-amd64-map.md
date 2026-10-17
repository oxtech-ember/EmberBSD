# amd64 crash-dump map allocation

The dump-offset function uses `calloc(count, element_size)` in the standard order.
The real GCC16 libkvm command rejects the previous reversed order.
The argument swap preserves the element count, size, zeroing and allocation-product checks.
There is one `calloc` call in this translation unit.

Both GCC `-fanalyzer` and Cppcheck also identify an unchecked allocation result in the old function.
On allocation failure, the function now searches the original RAM segments without allocating.
It adds the sizes of preceding segments to the dump offset.
It tests the address with subtraction, which avoids overflowing the range end.
An address outside all segments retains the existing `dump_off` result.
The normal cached-map search and subsequent allocation retry remain unchanged.

Returning `-1` alone would be unsafe here.
Existing callers pass the result directly to mapped-I/O offset arithmetic.
This change does not alter those generic libkvm interfaces.

## Run the regression

```sh
ruby ember/tests/kvm-amd64-map.rb
```

The runner extracts the production `p2o`, comparator and `_kvm_pa2off` definitions with their BSD license.
The host fixture models the amd64 kcore fields and controls the allocator.
It checks successful allocation, repeated allocation failure, retry and an empty segment list.
It checks segment boundaries, gaps and a high-address range whose exclusive end would overflow.
Failure paths must not reach the cached-map search.
The successful cache must allocate once and retain the expected file offsets.
The default run uses Clang ASan/UBSan in an automatic temporary directory.
Valgrind is unavailable on the macOS host; this is not a Valgrind check.

Set `HOST_CC` and `HOST_CFLAGS` to select another compiler or analysis mode.
The default `-Wno-sign-compare` matches the existing libkvm target flags for its signed loop index.
The recorded native GCC16 `-fanalyzer` run also executes all scenarios.
Clang Static Analyzer checks the generated fixture separately.
Three negative controls remove the NULL fallback, introduce overflowing range arithmetic or change the no-match result.
Each control fails with its expected sanitizer or assertion diagnostic.

## Evidence boundary

The exact failed target command passes after the argument swap, with only its output path relocated.
The complete target TU passes GCC16 `-fanalyzer` after the allocation-failure fix.
Cppcheck analyzes actual GCC-preprocessed TU inputs with the real target flags.
Its reports decrease from 16 to 12; all four allocation-failure NULL reports disappear.
The remaining cast, variable-scope and const-pointer style reports are inherited.
They remain with their source owners; no suppression is added.
The target-preprocessed fixture also receives Cppcheck analysis.
Its four reports are inherited casts, variable scope and a const-pointer suggestion in the extracted functions.

These checks cover the isolated amd64 translation unit and host function contracts.
A full library/base rebuild, target execution and hardware acceptance remain separate checks.
