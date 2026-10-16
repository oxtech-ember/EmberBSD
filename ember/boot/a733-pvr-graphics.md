# A733 PowerVR graphics development checkpoint

This branch prepares native graphics resources for BXM `36.56.104.183`.
It includes HWRT dataset handles and paired geometry/fragment DRM submissions.
These interfaces remain unaccepted on hardware.
The installed kernel passes three native Vulkan compute lifecycles.
Zink rendering, HDMI/KMS, and accelerated Plasma remain unaccepted on this platform.
Use the [native execution guide](a733-pvr-execution.md) for the implemented compute path.

## Prepared components

| Component | Software evidence | Integration boundary |
| --- | --- | --- |
| Geometry, fragment, and transfer stream decoders | 3,914 sanitizer checks; 1,320 comparisons with pinned Linux encoders | Paired geometry/fragment submission exists; transfer submission remains separate |
| Paired geometry/fragment queues, fragment and transfer execution | 199,607 sanitizer checks; 13 causal regressions | Paired submit caller exists; graphics execution remains unverified |
| HWRT firmware resource owner | 14,101 sanitizer checks; 82,377 configurations compared with pinned Linux | UAPI, handle owner, and build registration exist; hardware acceptance remains pending |
| Shared GEM and VM mapping holds | BO checks: 37,866 / 148,458; file checks: 2,158 / 127, for 4 KiB / 16 KiB hosts | HWRT handle owner retains these holds; runtime acceptance remains pending |
| Two-phase free-list growth | 8,457,268 sanitizer checks; 44 allocation-failure cases; 11 causal regressions | Device registry and GROW callbacks have host lifecycle contracts |

GCC16 target compilation, GCC path analysis, Clang analysis, and Cppcheck
supplement these checks. Existing header diagnostics, fixture model findings,
and analyzer path-budget limits remain recorded separately.
Valgrind does not support the macOS ARM64 test host.
ASan/UBSan checks are not Valgrind or physical DMA tests.
The complete kernel is installed and compute-tested as described below.

The command encoders and FWIF use the MIT alternative from Linux revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
Native ownership, synchronization, and tests are AI-assisted EmberBSD work.
Each adapted source retains its attribution and license.

Run the actual-source contracts from this checkout:

```sh
for name in pvr-render pvr-stream pvr-context pvr-hwrt pvr-freelist pvr-bo pvr-file pvr-submit; do
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
        sh "ember/tools/$name-contract.sh"
done
PVR_EXEC_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/pvr-exec-contract.sh
```

## Resume integration

1. Keep the accepted kernel, matched modules, DTBs, and recovery hashes together.
2. Prepare an offscreen Vulkan consumer with independent pixel readback.
3. Verify and implement native transfer submissions for image-to-buffer readback.
4. Test offscreen Vulkan rendering before Zink and PRIME sharing.
5. Recheck the main compute guards after each driver change.

The execution backend installs GROW handlers before it becomes available to clients.
Unknown firmware IDs and incomplete owners fail closed.
The submit path now identifies exact geometry-signal and fragment-wait pairs.
It uses the existing firmware `FENCE_PR` instead of waiting before publishing the producer.
The former host wait timed out before rendering in an actual-source regression.
Host contracts cover the fix; hardware graphics acceptance remains pending.

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
The source contracts check these rules. Physical execution remains unverified.

## Physical acceptance boundary

The kernel `4d9b68f3ec4866ad46810b4257daf4333ef10f38` is installed on the 4 GiB A733 Zero 3W.
Its exact artifact and three native compute passes are recorded in the [execution guide](a733-pvr-execution.md).
The explicit PLL600 stages pass before native activation. Observe-only boot remains unchanged.
This verifies compute regression coverage with the graphics code present.
It does not execute paired graphics, physical GROW, or a pixel readback oracle.

Mesa 26.2.4nb6 with Zink and the Vulkan compute consumer are prepared in
[Ports](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/powervr.md).
Their installation does not establish GPU execution.
Native PRIME ownership and the A733 DE3.52/TCON/HDMI/KMS path remain separate work.
An offscreen result does not establish a visible accelerated Plasma session.

## Firmware event dispatcher

The primitives originate at `795a20d364e6f7c32c161ce787bd57890cde2e22`.
The dispatcher and execution integration passed independent source review.
The installed compute regression does not exercise physical GROW events.

The dispatcher adds validated GROW dequeue and GROW_UPDATE transport, a bounded
32-event service drain, and `pvr_fw_events` / `set_events` callbacks.
It also adds `pvr_freelist_current_pages()` for a ready, quiet owner.
The device registry uses full 32-bit firmware IDs, separate from per-file handles.
A native red-black tree bounds lookup without truncating the final valid ID.
Registration reserves the owner slot before firmware creation can issue commands.
Successful destruction removes the entry before the owner slot is freed.
Failed cleanup keeps the entry, VM, pin, and existing orphan device reference alive.
One pending owner spans successful GROW preparation and its matching completion.
Failed preparation creates no pending owner; failed completion retains it.
Callbacks hold both existing gates and never acquire either gate recursively.
Host tests use the actual tree implementation and driver/execution sources.
They cover registration failure, two clients, ID boundaries, cleanup, and retained references.
Reconstruction requests and physical GROW execution remain unsupported or unverified.

The fixture defines `current_pages_tests()` and does not reacquire the held bridge gate.
The page-count tests run before their two backing buffers are freed.
The former order caused an ASan heap-use-after-free in `reset_state()`.
Fresh normal and ASan/UBSan free-list checks pass after correcting that order.
The unchanged dispatcher contracts also pass ASan/UBSan:
bridge 582 checks, service 9,069 checks, and fwprobe 15,548,668 checks.
This checks host boundary models, not physical firmware execution.

The complete installed source has target analysis with real kernel build flags and generated headers.
The scope and inherited diagnostics are summarized below.
The getter checks assert exact equivalence; mutations of all five gating conditions fail them.
Physical firmware must also establish whether KICK consumption precedes the
request/response dependency. Keep bounded failure if that assumption does not hold.

HWRT UAPI and paired `SUBMIT_JOBS` integration now exist as described below.
Their free-list references share the device-wide GROW callback registry.

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
handles, render-target indices or contexts before waits or target exposure.
External dependencies are consumed outside both gates. A fragment wait uses
firmware ordering only when its paired geometry signals the same retained
sync object, handle type, and timeline point. User handle equality is insufficient.
`pvr_context_render_pair()` emits `FENCE_PR` on the geometry timeline before
the fragment command. It publishes both queues in one combined KICK.
Both commands use the validated HWRT firmware address and cleanup control.
The fragment command type (FRAG or FRAG_PR) comes from the decoded stream.
Both results remain unpublished until queue completion and VM synchronization.

The shared graphics sender accepts only zero or `CMD_EXECUTED` after KICK transport completes.
Zero does not prove GPU completion. Both CCCB consumption and each UFO value remain required.
Fresh health checks, finite waits, failure retention, and strict CLEANUP acknowledgements remain unchanged.
Host contracts cover both return words for paired, fragment, and transfer senders.
The adopted native queue accepts `COMBINED_GEOM_FRAG_KICK` after validating both 32-byte payloads.
It preserves the single-KICK field checks and clears inactive cleanup entries and packet bytes.
Malformed payloads cause no DMA synchronization, publication, barrier, or doorbell write.
Named ABI assertions pin the geometry offset, fragment offset, and combined payload size.
The legacy diagnostic entry point still rejects combined KICK.
The queue retains its owner, firmware identity, consumed-entry checks, finite deadline, and failure state.
Transport completion does not replace CCCB/UFO completion in the context sender.
Host tests cover both return words, queue wrap, invalid halves, lost consumption, and firmware faults.
These contracts do not establish physical paired graphics or GROW execution.

The synchronous submit path cannot schedule an unpublished self or forward dependency.
It returns `EDEADLK` when a remaining job can publish the currently missing sync point.
A later timeline signal can satisfy an earlier point, but only an exact paired
point receives the internal `FENCE_PR` treatment. An already published external
fence retains its normal wait, even when a later job signals that object again.
The external fence can complete independently while this batch waits.
Failed external fences preserve their errors. Other external waits retain their
deadlines, interrupt handling, and retained references across handle removal.
Each batch permits at most `1 << 20` job and sync visits across producer searches.
Empty jobs also consume this budget. Exhaustion returns `E2BIG`.
These are synchronous implementation limits, not firmware or UAPI limits.
The returned job count still reports partial progress if an earlier job completed.

The actual-source submit contract checks exact binary and timeline dependencies,
duplicate points, distinct objects and types, external failures, cycles, and forward waits.
It also checks pair identity before target exposure, bounded searches, and HWRT lifetime.
Backend loss retains the entire batch until a separately proven reset permits release.
Normal and ASan/UBSan runs pass. The former host-wait behavior fails the new pair case.
These checks use kernel, DRM, and firmware boundary models; they are not hardware evidence.
The current full-branch build below includes these submit changes.

## Full kernel build and target analysis

The complete committed tree `4d9b68f3ec4866ad46810b4257daf4333ef10f38` builds with GCC16.2.0 and `EMBER64`.
The normal builder checks four matched modules and three DTBs. Its portable contracts pass.
Unchanged module dependencies permit incremental object reuse; the artifact manifest records their exact hashes.
The ELF and raw Image formats, BUILDINFO, source blobs, and executable modes are checked separately.

Target analysis covers 13 changed firmware/PowerVR units and 11 corresponding compute-baseline units.
The two new HWRT units have no invented baseline. All 24 GCC16 `-fanalyzer` compilations pass without diagnostics.
Clang23 reports only the same inherited dead stores in common headers for five DRM units.
Cppcheck2.22 parses the real GCC-preprocessed inputs but returns nonzero for retained findings.
Comparable units add style/information findings only. Common-header warnings and normal branch limits remain explicit.
No analyzer categories are disabled. These results supplement the source contracts and bounded compute check.
They do not establish physical paired graphics, transfer execution, or firmware GROW behavior.
