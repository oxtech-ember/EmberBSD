# AArch64 initial floating-point state

EmberBSD initializes a new AArch64 FP save area to zero: vector registers,
FPCR and FPSR. Scalar arithmetic starts with round-nearest, ties-to-even,
gradual underflow and NaN payload propagation. This matters to numeric
applications and compiler conformance tests.

The adaptation changes only `!PCU_VALID` in
[`fpu_state_load`](../fpu.c), retaining NetBSD's
PCU machinery. A valid context keeps its complete saved state, including
user-selected rounding, FP16 and AFP controls. Enable, barrier, restore,
counter and `PCU_REENABLE` behavior are unchanged. FP availability detection
and the separate arm32 VFP driver are unchanged.

MVFR1 describes AArch32 SIMD/VFP features. On an AArch64-only implementation
its value is UNKNOWN; zero does not mean native AArch64 needs DN or FZ.
The previous MVFR1-based initialization incorrectly set both controls on
an AArch64-only VM. Both the base and candidate compiler reproduced lost
subnormal results and NaN payloads; a process-local diagnostic using FPCR=0
produced the expected IEEE results and restored its original state.

Architecture references:

- [Arm ARM DDI0487A.k, D7.2.69, D7-2070](https://www.bitsavers.org/components/arm/ARM_Architecture_Reference_Manual_ARMv8_Rev_A.k_201609.pdf)
  defines MVFR1_EL1, including UNKNOWN on AArch64-only implementations
  (mirror of the original Arm manual).
- [Arm DDI0616 B.a, E3.2.7, pp.890–899](https://documentation-service.arm.com/static/6526e1bd9e189a266cef8412)
  defines FPCR controls, including FP16 and AFP. Absent optional controls
  are RES0; zero does not enable them. Warm-reset values can be UNKNOWN,
  so hardware reset is not the Unix initial-state contract.
- [AAPCS64 SIMD and floating-point registers](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#simd-and-floating-point-registers)
  permits user control of the FP environment; initializing a context must
  not become normalization of valid state on every restore.

## Regressions

Run from a source checkout:

```sh
sh ember/tools/aarch64-fp-state-contract.sh
FP_STATE_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/aarch64-fp-state-contract.sh
```

The host contract extracts the actual production function, save-area
layout and control constants. Kernel doubles record enable/barrier/load
ordering. Six groups cover zero and varied legacy MVFR values, full valid
state preservation and reenable without load. `FP_STATE_SOURCE` can name
an older `fpu.c` for the baseline RED check; that source is never installed.

On a native AArch64 system with FP support:

```sh
sh ember/tools/aarch64-fp-state-runtime.sh
```

The runner builds one userland executable and runs three independent modes:
fresh exec defaults, valid-state preservation, and exec reset from a parent
with deliberately selected FPCR/FPSR. Fresh-state checks never clear FPCR
before scalar float/double subnormal input/output, NaN and rounding tests.
Preservation covers fork, two threads with different FPCR/FPSR and yields,
and signal return after the handler deliberately changes its state. Each
process restores its own original controls/status after its probes.
Any failed mode makes the runner exit nonzero, even when preservation passes.
It neither changes another process nor adjusts system protection settings.

## Evidence boundaries

The extracted-production regression fails against the original MVFR-based
initialization and passes with the source correction, including host
ASan/UBSan. Userland runtime regression also passes on an AArch64 host;
that checks the probe, not an EmberBSD kernel.

The corrected EmberBSD AArch64 VM passed fresh-exec defaults, fork/signal/
two-thread preservation, and exec reset of deliberately changed parent state.
Fresh exec reported FPCR=0 and FPSR=0. The original VM reproduced the defaults
and exec-reset failure; the complete compiler suite from that environment
was not repeated in the corrected guest.

Physical Cortex-A76 checks passed the same three runtime modes on both
the original and corrected kernels. They provide hardware regression coverage,
not a causal reproduction of the VM failure. A host-only run validates the
probe, not an EmberBSD kernel. The
[pinned original record](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/ember/boot/aarch64-fp-state.md)
retains the source and execution identities for these distinct results.

The shared COMPAT_NETBSD32 save area maps FPSCR controls/status to FPCR/FPSR;
zero initial state is compatible with this mapping. AArch32 execution on a
CPU supporting AArch32 EL0 remains a separate pending check. Optional native
FP16/AFP arithmetic probes, FP-absent CPUs, SVE and SME are not covered here.
