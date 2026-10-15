# A733 PowerVR graphics development checkpoint

This branch prepares native graphics resources for BXM `36.56.104.183`.
It includes HWRT dataset handles and paired geometry/fragment DRM submissions.
These interfaces remain unaccepted on hardware.
No `VkDevice`, Vulkan dispatch, Zink rendering, HDMI/KMS, or accelerated Plasma
acceptance was performed with this checkpoint.
Use the [native execution guide](a733-pvr-execution.md) for the implemented compute path.

## Prepared components

| Component | Software evidence | Integration boundary |
| --- | --- | --- |
| Geometry, fragment, and transfer stream decoders | 3,914 sanitizer checks; 1,320 comparisons with pinned Linux encoders | Paired geometry/fragment submission exists; transfer submission remains separate |
| Paired geometry/fragment queues, fragment and transfer execution | 199,607 sanitizer checks; 13 causal regressions | Paired submit caller exists; graphics execution remains unverified |
| HWRT firmware resource owner | 14,101 sanitizer checks; 82,377 configurations compared with pinned Linux | UAPI, handle owner, and build registration exist; hardware acceptance remains pending |
| Shared GEM and VM mapping holds | BO checks: 37,866 / 148,458; file checks: 2,158 / 127, for 4 KiB / 16 KiB hosts | HWRT handle owner retains these holds; runtime acceptance remains pending |
| Two-phase free-list growth | 8,457,268 sanitizer checks; 44 allocation-failure cases; 11 causal regressions | Dispatcher contracts pass; device registry and callback installation are absent |

GCC16 target compilation, GCC path analysis, Clang analysis, and Cppcheck
supplement these checks. Existing header diagnostics, fixture model findings,
and analyzer path-budget limits remain recorded separately.
Valgrind does not support the macOS ARM64 test host.
ASan/UBSan checks are not Valgrind or physical DMA tests.
This source checkpoint has not produced an installed kernel.

The command encoders and FWIF use the MIT alternative from Linux revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
Native ownership, synchronization, and tests are AI-assisted EmberBSD work.
Each adapted source retains its attribution and license.

Run the actual-source contracts from this checkout:

```sh
for name in pvr-stream pvr-context pvr-hwrt pvr-freelist pvr-bo pvr-file; do
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
        sh "ember/tools/$name-contract.sh"
done
```

## Resume integration

1. Add the device-owned free-list registry and install its `set_events` callbacks.
2. Review paired submission dependencies, including a fragment wait on its paired geometry signal.
3. Integrate the current main compute fixes before a new graphics kernel build.
4. Build a clean committed kernel and its matched modules with the common GCC16 tools.
5. Recheck the compute path on the physical board before graphics.
6. Check offscreen Vulkan rendering and Zink before native display integration.

The dispatcher can process GROW events, but no execution-layer caller installs its handlers.
A GROW request without handlers fails closed.
The current submit path waits for both jobs before executing a pair.
Its existing pair fixture has no intra-pair sync dependency.
An actual-source test with a geometry signal and matching fragment wait times out before rendering.
The same pair without that dependency passes.
Fix this ordering before accepting paired dependencies.

Keep both gates held in firmware callbacks: device first, then platform.
A callback must not acquire the device gate again.
Growth publishes PFNs before its response; unknown completion must retain the pages.
Free-list reconstruction and GPU reset remain unsupported.
A later reconstruction implementation must preserve the order of prepended PFN blocks.
The append order of the native allocation descriptors is not that table order.

The HWRT handle wrapper retains local and global free lists.
Both lists and the rendering context must refer to the same VM.
Use shared mapping holds for the two MLIST and region-header spans packed in one BO.
Do not weaken the exclusive free-list backing pin.
Release holds and free-list references only after acknowledged HWRT cleanup.
An uncertain cleanup retains the entire owner and its dependencies.

The submit path performs a full-batch copy, decode, and reference pass.
Pair adjacent geometry and fragment jobs only after validating their dependency,
context, HWRT owner, VM, and target index.
Publish both queues with `pvr_context_render_pair()` before waiting for geometry.
A matched intra-pair dependency needs firmware ordering; external waits must remain outside both gates.
Publish both results only after both queues complete and VM synchronization succeeds.
Standalone partial render and unpaired geometry need explicit rejection until implemented.
These dependency rules remain acceptance requirements for the new UAPI path.

## Physical acceptance boundary

The main compute path has separate [execution evidence](https://github.com/oxtech-ember/EmberBSD/blob/main/ember/boot/a733-pvr-execution.md).
This graphics branch has no physical graphics acceptance.
Recheck board identity, current kernel, and recovery before any deployment.
A saved build is not evidence that its kernel is installed.

Mesa 26.2.4nb6 with Zink and the Vulkan compute consumer are prepared in
[Ports](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/powervr.md).
Their installation does not establish GPU execution.
Native PRIME ownership and the A733 DE3.52/TCON/HDMI/KMS path remain separate work.
An offscreen result does not establish a visible accelerated Plasma session.

## Firmware event dispatcher

The primitives originate at `795a20d364e6f7c32c161ce787bd57890cde2e22`.
The dispatcher and execution integration are later branch changes.
They require independent review before integration into `main` or deployment.

The dispatcher adds validated GROW dequeue and GROW_UPDATE transport, a bounded
32-event service drain, and `pvr_fw_events` / `set_events` callbacks.
It also adds `pvr_freelist_current_pages()` for a ready, quiet owner.
The device-wide registry and installation of its callbacks are not implemented.
Reconstruction requests remain unsupported.

The fixture defines `current_pages_tests()` and does not reacquire the held bridge gate.
The page-count tests run before their two backing buffers are freed.
The former order caused an ASan heap-use-after-free in `reset_state()`.
Fresh normal and ASan/UBSan free-list checks pass after correcting that order.
The unchanged dispatcher contracts also pass ASan/UBSan:
bridge 582 checks, service 9,069 checks, and fwprobe 15,548,668 checks.
This checks host boundary models, not physical firmware execution.

Target analysis for the draft: Clang Static Analyzer is clean on the service,
bridge and freelist sources; Cppcheck reports three variable-scope style notes
in the service and one false-positive `uninitvar` at the post-loop read of
`error` (the loop body always assigns it first). A separately recorded target
GCC16 path analysis is still needed for the dispatcher itself. The getter checks assert exact
equivalence, so any mutation of the five gating conditions fails them. Review them before claiming a complete dispatcher.
The earlier passing growth receipt does not cover this later getter change.
Physical firmware must also establish whether KICK consumption precedes the
request/response dependency. Keep bounded failure if that assumption does not hold.

HWRT UAPI and paired `SUBMIT_JOBS` integration now exist as described below.
They do not install the missing device-wide GROW callback registry.

## HWRT execution ownership

`pvr_exec_hwrt.c` owns HWRT dataset handles and references. The pinned
upstream UAPI (create `0x0b`, destroy `0x0c`) is now in `pvr_drm.h`.
Create resolves two distinct free-list handles from one VM, keeps both
references for the dataset lifetime, verifies the local initial size
against `pvr_device_hwrt_info()`, recomputes the region-header and
tail-pointer-cache spans from the pinned profile formulas and rejects
user TPC size/stride mismatches, then places shared holds with the
pinned cache policy (MLIST adds PM firmware protection). Destroy and
the last reference put release holds only after the firmware cleanup
acknowledgement; a failed cleanup retains the owner, both free lists,
their VM and every hold through device orphan retention. `target` marks
a render target exposed before the first KICK and accepts index 0 or 1.

The geometry formulas live in `pvr-exec-hwrt-fixture.h` and are checked
against an independent recomputation from the Mesa formulas (`pvr-exec
-hwrt-contract.sh`); the MLIST input is a page count, not bytes. Cppcheck
and Clang Static Analyzer are clean on the new module.
The source and build checks do not establish physical HWRT lifetime.

## Paired submission

`pvr_submit_jobs` now decodes geometry and fragment jobs beside compute.
Geometry and fragment must alternate strictly: every geometry is followed
by its fragment, and no fragment appears alone. The pair resolves one
HWRT reference (kept on the geometry half; the fragment reuses it, so
exactly one get and one put per pair), checks that the dataset, the
render context and both jobs share one VM, and rejects differing dataset
handles, render-target indices or contexts. Both jobs' wait dependencies
are consumed while ungated before execution; the target is marked
exposed immediately before the paired call, both commands receive the
firmware HWRT data address, and `pvr_context_render_pair` publishes both
queues in one combined kick with the target's cleanup control. The
fragment command type (FRAG or FRAG_PR) comes from the decoded stream.
The contract covers the successful pair, the lone-fragment rejection and
the render-context preflight; Cppcheck and Clang Static Analyzer are
clean on the changed sources.

## Full branch build and target analysis (2026-10-14)

The complete branch kernel built clean from the committed tree at
`dd8cc15b8df1` (GCC16 `EMBER64`, four matched modules, DTBs retained);
the build receipt with output hashes is in the incremental build root.
The review pass before it fixed four compile-visible defects: a missing
type for the HWRT handle drain, an unavailable `sys/limits.h`, an opaque
free-list dereference replaced by exported identity and page-count
getters, and the device-info width plus `kpause` declaration. GCC16
`-fanalyzer` ran on `pvr_exec_hwrt.c`, `pvr_submit.c` and `pvr_exec.c`
with the exact build flags: zero warnings. The step-2 code is
build-verified only; no hardware ran it.
