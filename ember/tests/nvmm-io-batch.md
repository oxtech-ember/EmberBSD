# NVMM batch I/O buffer lifetime

The batch helper temporarily points `nvmm_io.data` at its local transfer buffer.
The old function leaves that pointer in the caller's descriptor after returning.
GCC16 rejects the escaping stack address under the real libnvmm `-Werror` flags.
The compiled translation unit is `libnvmm.c`, which includes `libnvmm_x86.c`.

The helper now saves the original data pointer and restores it through one cleanup path.
Normal completion, guest-read failure and guest-write failure all use that path.
The cleanup does not call another function, so it preserves the failing helper's `errno`.
Callbacks retain the same descriptor address and the same per-element buffer positions.
Only the data pointer is restored; other callback field changes remain visible.
A local descriptor copy would not preserve those properties.
No public structure, callback signature or ABI changes.

## Run the regression

```sh
ruby ember/tests/nvmm-io-batch.rb
```

The runner extracts the production helper, constants and public I/O descriptor.
It retains the source BSD license and models guest-memory access and VCPU callback plumbing.
The 44 cases cover input, output, guest-memory errors and zero, short and capped batches.
They check callback identity, buffer contents, callback field changes, return counts and `errno`.
They also check the restored caller buffer after return.
The default native run uses Clang ASan/UBSan and removes its temporary directory.
Valgrind is unavailable on this macOS host; this is not a Valgrind check.
Set `HOST_CC` and `HOST_CFLAGS` to select GCC16 analysis or Clang Static Analyzer.

Four negative controls remove restoration, bypass cleanup on either error path or copy the descriptor.
Each control fails its expected assertion.

## Evidence boundary

The exact failed target command fails before the fix and passes after it.
Only the output path is relocated for the replay.
The extracted helper also passes native GCC16 `-fanalyzer`, execution and Clang Static Analyzer.
Cppcheck uses actual GCC-preprocessed target TU inputs.
It decreases from 128 to 127 reports, removing the batch helper's escaping-address report.
Ten other escaping-address reports and the remaining allocation/style findings remain with the libnvmm owner.
The full-TU GCC analyzer retains inherited list-removal and machine-creation findings.
It is not a clean analysis result, and no suppression is added.
Those findings require separate ownership and API checks.

These checks cover the helper and isolated target compilation.
They do not exercise `/dev/nvmm`, a guest VM or physical hypervisor support.
