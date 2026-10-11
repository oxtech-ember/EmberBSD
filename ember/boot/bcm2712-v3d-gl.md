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
The selected presentation target is a remote desktop: a virtual Wayland output
with hardware rendering and remote input/capture, after offscreen OpenGL passes.

- [x] Add actual-source CL contracts for validation, both dependencies,
  completion IRQ identity/count, cache order, memory ownership and failure.
  Confirm rejection before adding the implementation. Preserve TFU/CSD tests.
- [x] Implement CL admission/execution in the native driver and ioctl table.
  Check optional binning, repeated jobs, same-handle fences, close races and
  every failing register operation. Run static and memory analysis.
- [x] Cross-build a matched kernel and userspace clear/store acceptance tool.
  Review ownership and MMIO sequencing before physical execution. Preserve
  and verify the ordinary kernel/module bundle for recovery.
- [x] Add V3D to the canonical current Mesa recipe through EmberBSD-Ports.
  Repair actual portability failures and keep existing VirGL/LLVM consumers.
  Build and test the package using recorded EmberBSD sysroot provenance.
- [x] Test real EGL/GL V3D rendering on CM5. Fix the first causal failure,
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
The subsequent hardware CL result is recorded below; Mesa acceptance remains
a separate gate.

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
binary fences, two clients and CL/TFU/CL transitions.

## Physical CL acceptance, 2026-10-11

At 13:29:45 UTC, CM5 Rev 1.0 / BCM2712 D0 (4 GiB, 64 GB eMMC) on the
Waveshare CM5-NANO-B passed the checker in 0.55 seconds, exit 0. The existing
UEFI was preserved; its source and carrier PCB revisions remain unknown.
All 325 bin/render pairs, 1,331,200 pixel values, guards, fences, two clients
and 32 TFU copies passed. No engine-error diagnostic appeared in the log.
This short clear/store check is not a shader, Mesa, KMS or desktop result.

GCC 16.2.0 cross-built `EMBERV3DRENDER` from commit
`246b46a2a718b7f4bba24b1e7423f6031fb95c5b` on macOS. Source export,
compiler provenance, ELF/Image/debuglink/CTF and matching module imports
passed verification. The ordinary kernel/module backups remain preserved.
The checker uses the previously recorded partial device ABI inputs; the
installed base revision remains unknown, so this is not full SDK acceptance.

| Artifact | SHA256 |
| --- | --- |
| Kernel ELF | `faa71db5d35c65720d16e9307f6c89a3b42842dd1971ce2ab21638734c8dca30` |
| AArch64 Image | `ea66e528e9b3f95e54c47c6da2ae09972a5209c72bd223d8b0ca580e164ed4ce` |
| Static check | `65d19a58340099f6c3bc957a3b115e0e24537e41e5f153134792a4254308472c` |
| Full kernel/test log | `546ffcb79e91b7b738007efb4dfd4561b0a2676a4096c29e36c1d1e5511b3e31` |

Cppcheck 2.22.0 with actual target preprocessing retained 17 findings each
in the job/driver units, with no new diagnostics against the accepted sync
baseline. GCC 16.2 Analyzer emitted none. Apple Clang 21 retained the same
six inherited header/macro findings. Valgrind is unavailable on these ARM64
hosts; ASan/UBSan covered the host model instead. These tools do not prove
device cache behavior. The hardware pixel/guard check supplies that evidence.

Protocol sources: the existing queue/IRQ probes and their pinned Linux
register references, plus the original Mesa 26.2.4 archive. Preserve licenses
and attribution. The ordinary kernel remains unchanged until an accepted
integration is explicitly installed; experimental bundles are identified.

## Physical Mesa acceptance, 2026-10-11

The matched `EMBERV3DRENDER` build from `47953f7f97c` and Ports
MesaLib 26.2.4nb3/libdrm 2.4.134nb2 pass installed V3D OpenGL on the same
CM5 after normal reboot. Native platform metadata removes the earlier
`eglInitialize` failure. Both explicit render-node and default surfaceless
discovery select `V3D 7.1.10.2`, reporting EGL 1.5 and GLES 3.0.

The consumer rejects invalid GLSL, compiles real vertex/fragment shaders,
checks clear/triangle/outside pixels and destroys objects/context. Four cycles
pass at 16x16 and four at 1280x800; the latter take 0.19 seconds total.
This duration is a short lifecycle check, not a desktop frame-rate benchmark.
All 38 selected Mesa upstream invocations pass, with one internal utility skip
and nine disabled NIR cases. Explicit software comparison also passes four
llvmpipe/LLVM23 cycles. See the Ports
[package receipt and commands](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-cm5.md).

Kernel ELF SHA256: `889fc52e3b083486414a5473e1626dd035b3093ac295600031d476b6a2872553`.
Image SHA256: `bf3963a0a0eb01714d8065eb6747a4b6ed9702b0a307531eee68cc9edb1c7f91`.
All four companion modules are byte-identical to the accepted CL bundle.
The ordinary bundle remains preserved. The experimental driver still limits
individual BOs to 16 MiB and total allocation to 64 MiB. Sharing buffers between
clients, a virtual Wayland output, remote input/capture and the current common
Qt/KWin/Plasma package migration are subsequent gates; HDMI is not required
for the selected remote desktop scenario. No long-run desktop claim follows.
