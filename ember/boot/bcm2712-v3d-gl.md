# Native CM5 OpenGL integration

Goal: run hardware OpenGL through the common Mesa package on CM5, then use
that same provider for KWin/Plasma. The user's selected desktop target is CM5.
This continues the accepted [binary synchronization](bcm2712-v3d-sync.md).

## Design and implementation plan

Execute inline. Preserve the existing serialized job lock, per-submission
GEM references, MMU table, cache protocol and poison retention. Add standard
SUBMIT_CL for optional binning followed by rendering. Both input fences must
be snapshotted before the output replacement. Validate command-list ranges,
tile allocation/state addresses, copied handles and unsupported extensions.
Use the accepted native IRQ ownership, with the expected completion selected
for each phase. An OOM or uncertain failure stops the engine and retains BOs;
automatic overflow allocation and arbitrary-job recovery are not promised.

The first hardware gate is a userspace clear/store command list with guarded
buffers. The next is real Mesa shaders and pixel readback, with explicit V3D
identity and no software fallback. Rendering and display/KMS are separate:
Plasma requires a usable presentation path after offscreen OpenGL passes.

- [x] Add actual-source CL contracts for validation, both dependencies,
  completion IRQ identity/count, cache order, memory ownership and failure.
  Confirm rejection before adding the implementation. Preserve TFU/CSD tests.
- [x] Implement CL admission/execution in the native driver and ioctl table.
  Check optional binning, repeated jobs, same-handle fences, close races and
  every failing register operation. Run static and memory analysis.
- [ ] Cross-build a matched kernel and userspace clear/store acceptance tool.
  Review ownership and MMIO sequencing before physical execution. Preserve
  and verify the ordinary kernel/module bundle for recovery.
- [ ] Add V3D to the canonical current Mesa recipe through EmberBSD-Ports.
  Repair actual portability failures and keep existing VirGL/LLVM consumers.
  Build and test the package using recorded EmberBSD sysroot provenance.
- [ ] Test real EGL/GL V3D rendering on CM5. Fix the first causal failure,
  retaining logs and regressions. Rendering success must include pixel checks.
- [ ] Connect presentation, then current KWin/Plasma; check renderer identity,
  input, repaint and repeated session lifecycle. Keep unaccepted stages explicit.

## Global constraints and review focus

Current Mesa is 26.2.4 (upstream release index checked 2026-10-11); reuse the
common LLVM23/GCC16 dependencies. Cross-build on the development host and
execute on CM5. Do not install a second permanent graphics stack. Keep source
and reusable checks in their owning public repository, with no private paths.

Review command ranges crossing BO boundaries, BO handle closure during a
wait, stale or duplicate IRQs, changed page tables with live GPU references,
and CPU ownership returned before cache clean. Include each in the CL tests.
The native driver must not advertise unimplemented feature bits. A compiled
Mesa driver does not prove GL support or an accelerated desktop.

The CL host model passes 51,611 ownership/IRQ checks, 64 injected register
failures and nine rejected causal mutants. ASan/UBSan passes. CSD, TFU,
binary syncobj, BO and UAPI regressions pass. Fresh review exposed a dependency
snapshot race: closing an aliased syncobj in the first input wait prevented
capture of the second input. The regression failed before the repair. Both
input fences and the output reference are now retained before any wait.
These results do not establish hardware CL or Mesa acceptance.

## Reproduce the native CL gate

Run `sh ember/tools/bcm2712-v3d-cl-contract.sh` on the build host. Cross-build
`EMBERV3DRENDER`, then build the static checker from the same source checkout:

```sh
CC=/absolute/cross/bin/aarch64--netbsd-gcc SYSROOT=/absolute/emberbsd-sysroot \
    sh ember/tools/bcm2712-v3d-cl-check-build.sh /absolute/source /absolute/new-output
```

The builder extracts the unchanged, accepted queue-probe packet generator.
Record the actual base headers, libraries and compiler provenance. On CM5,
run `./cl-check /dev/dri/renderD128` with the matched kernel/module bundle.
It checks 325 bin/render pairs, 1,331,200 pixels, command/BO guard regions,
binary fences, two clients and CL/TFU/CL transitions. These are intended
acceptance counts until a successful target execution is recorded.

Protocol sources: the existing queue/IRQ probes and their pinned Linux
register references, plus the original Mesa 26.2.4 archive. Preserve licenses
and attribution. The ordinary kernel remains unchanged until an accepted
integration is explicitly installed; experimental bundles are identified.
