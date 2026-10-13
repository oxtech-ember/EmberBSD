# A733 PowerVR graphics development checkpoint

This checkpoint prepares native graphics resources for BXM `36.56.104.183`.
It does not enable graphics submissions through the DRM UAPI.
The current public submission path still accepts compute jobs only.
No `VkDevice`, Vulkan dispatch, Zink rendering, HDMI/KMS, or accelerated Plasma
acceptance was performed with this checkpoint.
Use the [native execution guide](a733-pvr-execution.md) for the implemented compute path.

## Prepared components

| Component | Software evidence | Integration boundary |
| --- | --- | --- |
| Geometry, fragment, and transfer stream decoders | 3,914 sanitizer checks; 1,320 comparisons with pinned Linux encoders | Submit still rejects these job types |
| Paired geometry/fragment queues, fragment and transfer execution | 199,607 sanitizer checks; 13 causal regressions | No DRM graphics submit caller |
| HWRT firmware resource owner | 14,101 sanitizer checks; 82,377 configurations compared with pinned Linux | No HWRT UAPI, handle owner, or kernel build registration |
| Shared GEM and VM mapping holds | BO checks: 37,866 / 148,458; file checks: 2,158 / 127, for 4 KiB / 16 KiB hosts | HWRT handle owner must retain these holds |
| Two-phase free-list growth | 8,441,862 sanitizer checks; 44 allocation-failure cases; 11 causal regressions | Firmware event dispatch and device registry are separate |

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

Run the actual-source contracts on the tested primitives revision:

```sh
for name in pvr-stream pvr-context pvr-hwrt pvr-freelist pvr-bo pvr-file; do
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
        sh "ember/tools/$name-contract.sh"
done
```

## Resume integration

1. Finish and review the firmware GROW event dispatcher.
2. Add a device-owned free-list registry and reference-counted free-list handles.
3. Add HWRT UAPI, handle ownership, mapping validation, and build registration.
4. Extend submission preflight and execution for paired graphics and transfer jobs.
5. Build a clean committed kernel and its matched modules with the common GCC16 tools.
6. Check the compute path on the physical board before graphics.
7. Check offscreen Vulkan rendering and Zink before native display integration.

Keep both gates held in firmware callbacks: device first, then platform.
A callback must not acquire the device gate again.
Growth publishes PFNs before its response; unknown completion must retain the pages.
Free-list reconstruction and GPU reset remain unsupported.
A later reconstruction implementation must preserve the order of prepended PFN blocks.
The append order of the native allocation descriptors is not that table order.

The proposed HWRT handle wrapper needs retained local and global free lists.
Both lists and the rendering context must refer to the same VM.
Use shared mapping holds for the two MLIST and region-header spans packed in one BO.
Do not weaken the exclusive free-list backing pin.
Release holds and free-list references only after acknowledged HWRT cleanup.
An uncertain cleanup retains the entire owner and its dependencies.

The proposed submit integration needs a full-batch copy, decode, and reference pass.
Pair adjacent geometry and fragment jobs only after validating their dependency,
context, HWRT owner, VM, and target index.
Publish both queues with `pvr_context_render_pair()` before waiting for geometry.
The matched intra-pair dependency becomes `FENCE_PR`; other waits occur outside both gates.
Publish both results only after both queues complete and VM synchronization succeeds.
Standalone partial render and unpaired geometry need explicit rejection until implemented.
These are proposed integration rules, not available UAPI behavior.

## Physical acceptance boundary

The last accepted Zero 3W kernel is `f46cd40a5f28c75066b92f169d6ce981b3cb0253`.
It includes the USB series and passes the diagnostic compute packet and Mesa GPU discovery.
The newer compute kernel built from `f32b310f5ea3c2bb6fd3a1934b9311f1e39521fc`
has not been installed or accepted on that board.
The board is currently outside the test host's reachable network.
Recheck board identity, current kernel, and recovery before any deployment.

Mesa 26.2.4nb6 with Zink and the Vulkan compute consumer are prepared in
[Ports](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/powervr.md).
Their installation does not establish GPU execution.
Native PRIME ownership and the A733 DE3.52/TCON/HDMI/KMS path remain separate work.
An offscreen result does not establish a visible accelerated Plasma session.
