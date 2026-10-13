# A733 compute VM context

The [VM encoder](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_vm.c)
builds the GPU page tables and a 32-byte firmware memory context.
It initializes every context byte on success.
The context contains the page-catalog physical address and the invalid PCSET
marker. Unused breakpoint fields, context flags, and padding are zero.

The layout follows the MIT-licensed Linux PowerVR
[`rogue_fwif_fwmemcontext`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/imagination/pvr_rogue_fwif.h?id=7b63ef2d55f24519e7e9e5f4d15dbea03f126e40).
Its fields occupy offsets 0, 8, 12, 16, 20, and 24. Padding starts at offset 28.
The [native heap owner](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.c)
copies all 32 bytes to firmware-visible memory after encoding succeeds.
Initializing only the address and PCSET leaves bytes 12 through 31 unspecified.

On failure, the encoder leaves the firmware context unchanged.
Other output buffers and the summary can contain partial results.
The caller must discard them.
The native heap owner frees its unpublished allocations after an encoder error.
The host packer stops before it emits an image after such an error.

Run the real-source regression from the repository root:

```sh
sh ember/tools/a733-gpu-fwcompute-vm-contract.sh
sh ember/tools/a733-gpu-fwfile-contract.sh
```

The first command uses ASan and UBSan by default.
It fills the context with nonzero bytes, checks the complete output, and checks
the surrounding guards. It repeats the call with another catalog address.
It also verifies that early and late errors preserve the context.
The test links the actual encoder and MMU helpers.
`CC` selects the host C compiler. `FWCOMPUTE_VM_TEST_CFLAGS` selects test flags.
`FWCOMPUTE_VM_SOURCE` selects a saved encoder source for a causal regression.
The second command also checks the existing image packer with the VM encoder.

The context regression has 36 checks. The previous encoder fails both poisoned
context checks. Moving context initialization before validation fails all six
error-preservation checks.

Target analysis uses the generated `EMBER64` headers and the kernel GCC16 compile
configuration. Clang 22 Static Analyzer reports no diagnostics for either encoder.
Cppcheck 2.22 reports the same unnamed-parameter and isolated-translation-unit
unused-function diagnostics before and after this change.
GCC `-fanalyzer` reports existing path-complexity limits in both versions.
These limits prevent a claim of exhaustive path coverage.

The executable contract runs on macOS arm64 with ASan and UBSan.
It is not a Valgrind run or a device execution test.
It proves deterministic context serialization and error behavior.
It does not establish the cause of a GPU page fault or completed computation.
See the [compute submission boundary](a733-gpu-fwcompute.md) for device evidence.
