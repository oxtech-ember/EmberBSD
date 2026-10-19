# Early framebuffer output reader hooks

`WSDISPLAY_EARLYFB_HOOKS` is an opt-in source-only hook. No default kernel
configuration selects it. MI wsdisplay owns its state. The ARM FDT frontend
calls MI; MI has no dependency on an ARM header or frontend symbol.

This slice tracks actual early console output and the complete initial replay.
It does not close output, run a grace-period writer, detach a console, stop a
device or grant display ownership. It supplies no claim token, fallback issuer,
ready setter, MMIO permission, native display caller or firmware request.

## Binding and output

The [MI wrapper](../../sys/dev/wscons/wsdisplay.c) accepts the same valid binding
arguments as `wsdisplay_preattach`, plus a synchronous replay callback and its
argument. The caller preserves existing boot attachment serialization. It keeps
all callback data stable. The callback can execute a renderer. It must not sleep.
MI does not retain the callback or argument after return.

The wrapper refuses NULL required arguments, panic, interrupt/softinterrupt
context, an existing console and a repeated invocation. It publishes its exact
type/cookie pair before the real preattach can publish `cn_tab`. It checks the
actual MI binding after preattach. It then holds a pserialize read section over
the whole replay callback. READY follows only its return, a fresh binding check
and absence of panic. An error after binding does not undo that binding.

`wsdisplay_early_output_snapshot` returns actual binding/replay facts. It exposes
no caller-supplied identity token. Its output stays unchanged before registration
or on a NULL output pointer. A changed/runtime binding reports neither BOUND
nor READY. Panic never reports READY. These facts are not access admission.

`wsdisplay_cnputc` enters a read section before checking the exact early binding.
The section covers the real emulation output and old-console forwarding. A
runtime device, runtime attachment or foreign binding uses existing output
behavior without being classified as the early reader. Panic output remains
available. Keyboard and old-console `pollc` pairs remain unchanged.

The [ARM adapter](../../sys/arch/arm/fdt/arm_simplefb.c) invokes the real
`vcons_replay_msgbuf`. Its outer reader covers the final `vcons_update_screen`,
which follows the last replayed character. It refuses bad context or a repeated
published wrapper before mapping or changing rasops state. The option-off path
uses the existing preattach and replay calls.

## Bootstrap boundary

ARM `initarm` calls early `consinit` before `main`. The simplefb console selector
at that point records its FDT node and genfb console flag. It does not call
`arm_simplefb_preattach`. Actual preattach occurs through the armfdt device
registration path during `cpu_configure`, after `main` calls `pserialize_init`.
This path can run at high IPL. Hook registration uses static storage and atomic
operations. It creates no pserialize object or sleep mutex there.

`vcons_earlyinit` supplies `enable_intr=0`. This does not create the ordinary
vcons rendering thread/softinterrupt route. Reader hooks still do not prove
that every future framebuffer writer is covered. A generic non-sleeping replay
callback remains a caller obligation.

There is no successful close/drain API. The default-off
[checked publication foundation](console-publication.md) covers selected runtime
binders. Irreversible console commitment, rollback,
an independent output sink and firmware handoff are separate requirements.
`genfb_disable` is not detach/drain. A future issuer must also account for actual
render and fallback `config_finalize` ordering. No fallback state changes here.

## Source checks

```sh
ruby ember/tools/earlyfb-hooks-contract.rb /absolute/os/source
```

The runner extracts complete unchanged production functions and exact state/API
blocks. It preserves their licenses. It tests both option states and both
MULTICONS states. Only kernel/FDT/rasops/atomic/pserialize services are controlled.
Host threads pause controlled output and final-update callbacks. These pauses
do not permit a sleeping kernel callback. The model checks
reader lifetimes, nested replay/output, late output, factual registration,
runtime/foreign binding exclusion, panic, re-entry and balanced poll callbacks.
Compiled mutants must reach the fixture's fatal check. A compile failure, crash
of another kind or timeout is not a successful mutant rejection.

`CC` and `CFLAGS` select the compiler and flags. `TEST_RUNNER` is one executable
wrapper for all test runs. `EARLYFB_COMPILE_ONLY=1` compiles without execution.
`EARLYFB_SKIP_MUTANTS=1` runs positives. `EARLYFB_MUTANTS` selects named cases.
`EARLYFB_EVIDENCE` must name a new output directory. It saves exact source hashes,
extracted inputs, commands and output. These checks do not establish real kernel
MP grace-period behavior, a coherent kernel link, device drain or physical display
handoff. Use matching selected headers and actual compiler flags for native TU
analysis. Do not write private option overrides to a shared kernel object tree.
