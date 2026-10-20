# A733 native PowerVR graphics

The native driver supports graphics resources for BXM `36.56.104.183`.
It includes HWRT datasets, paired geometry/fragment jobs, a final fragment,
and transfer submission. Bounded offscreen and hardware KWin/Zink checks pass.
Use the [execution guide](a733-pvr-execution.md) for activation, firmware
ownership, compute acceptance and the exact kernel artifact.

## Physical acceptance boundary

The physical 4 GiB A733 Zero 3W runs GCC16.2.0 `EMBER64` kernel
`538697de0806efed208bb71de32d1fab42104e02` with matching modules and DTBs.
The PCB revision was not recorded. Firmware is `1.1.6976702`.
All 21 GPU diagnostic stages and three Vulkan compute lifecycles pass.
Each compute lifecycle verifies all 16 output words, a fence and host readback.

The offscreen Vulkan check draws a triangle into an optimal image and uses
transfer submission for readback. All 256 RGBA pixels match the reference.
Row padding and guard bytes remain intact. The maximum sampled temperature
across the diagnostic, compute and offscreen run is 54.56 degrees Celsius.

KWin 6.8 and a Qt Quick client use hardware OpenGL ES through PowerVR and
Zink on a virtual output. The separately staged Mesa 26.2.4nb8 runtime does
not replace the installed system Mesa package. Four uniform red, green,
blue and white frames of 640 by 480 pixels pass exact capture comparison:
1,228,800 pixels in total. The client and compositor exit successfully.
No DMA-BUF sync-file import/export error appears in these four frames.
Boot time and checked system hashes remain unchanged. The maximum sampled
temperature during this check is 54.312 degrees Celsius.
The public [KWin frame consumer](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/kwin-frame-check)
checks the client renderer and the compositor's captured pixels separately.

This uniform-frame result does not accept the separate one-LSB differences
at texture boundaries in the quadrant image. It does not repeat the earlier
80-cycle PRIME or 128-cycle sync-file checks from other kernel receipts.
See the [PRIME mechanism](a733-pvr-render.md#prime-sharing) and
[sync-file probe scope](drm-dmabuf-sync-check.md).
No tolerance is added to the pixel comparison.

Native HDMI/KMS, pending scanout retirement, a complete Plasma session,
physical free-list GROW events, reset recovery and endurance remain unaccepted.
The DE attempt on this kernel stops at step 4: global IOMMU flush register
0x30 returns 0 after a request of 3. The [cold-init repair](a733-iommu1-cold-init.md)
accepts that observed request state and keeps the final explicit write of 0.
Its source, GCC16 and host checks pass; its physical effect is not yet accepted.
Neither readback nor the BSP delay proves flush completion.
Mesa still requires `PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1`.
These results do not establish Vulkan conformance or general graphics stability.

## Source contracts and provenance

| Component | Executable contract scope |
| --- | --- |
| Geometry, fragment and transfer decoders | Compare encoded streams with pinned Linux encoders; reject malformed layouts |
| Paired queues and execution | Check ordering, queue wrapping, completion, timeouts and failure retention |
| HWRT owner | Compare profile geometry with pinned formulas; check references and cleanup |
| GEM and VM mapping holds | Check aliases, file lifetime, shared spans and backing pins |
| Free-list growth | Check allocation failures, PFN publication, owner registry and callbacks |

GCC16 target compilation and path analysis, Clang Static Analyzer, and
Cppcheck supplement these contracts. Inherited header findings and analyzer
path limits remain separate. The macOS ARM64 host cannot run Valgrind.
ASan/UBSan tests are not Valgrind or physical DMA tests.

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

## Firmware event dispatcher

The backend installs GROW handlers before it becomes available to clients.
The dispatcher validates GROW dequeue and GROW_UPDATE transport and bounds
each service drain to 32 events. `pvr_fw_events` and `set_events` connect
the callbacks. `pvr_freelist_current_pages()` requires a ready, quiet owner.

The device registry uses full 32-bit firmware IDs, separate from file handles.
A native red-black tree bounds lookup without truncating the final valid ID.
Registration reserves an owner slot before firmware creation can issue commands.
Successful destruction removes the entry before the owner slot is freed.
Failed cleanup retains the entry, VM, pin and orphan device reference.
One pending owner spans GROW preparation and its matching completion.
Failed preparation creates no pending owner; failed completion retains it.

Callbacks hold the device gate, then the platform gate. They do not acquire
either gate recursively. Growth publishes PFNs before its response.
Unknown completion retains the pages. A future reconstruction path must
preserve prepended PFN-block order, which differs from allocation order.
Reconstruction and GPU reset remain unsupported. Host contracts cover
registration failure, multiple clients, ID boundaries, cleanup and retention.
Physical GROW and its request/response timing still need acceptance.

## HWRT execution ownership

`pvr_exec_hwrt.c` owns dataset handles and references. The pinned upstream
create and destroy operations are 0x0b and 0x0c in `pvr_drm.h`.
Create resolves two distinct free lists in one VM and retains both.
It checks the local initial size against `pvr_device_hwrt_info()`.
It recomputes region-header and tail-pointer-cache spans from the profile
formulas and rejects mismatched user sizes or strides.

Shared mapping holds protect the two MLIST and region-header spans in one BO.
The MLIST input is a page count. Its hold adds PM firmware protection.
The exclusive free-list backing pin remains required.
Destroy and the last reference release these holds only after acknowledged
firmware cleanup. Failed cleanup retains the dataset, both free lists,
their VM and every hold. `target` records exposure before the first KICK
and accepts index 0 or 1. Host geometry checks independently recompute the
Mesa formulas. The bounded pixel checks do not establish every HWRT lifetime.

## Paired submission

`pvr_submit_jobs` copies and validates the complete batch before publication.
Each geometry job must have its paired fragment. The pair retains one HWRT
reference and uses one context, VM, dataset and target index.
An exact fragment wait on its geometry signal uses firmware `FENCE_PR`.
The retained sync object, handle type and timeline point must all match.
User handle equality alone is insufficient. External waits stay outside
the device and platform gates.

`pvr_context_render_pair()` publishes both queues in one combined KICK.
It emits GEOM, FENCE_PR, FRAG or FRAG_PR, and the required UFO updates.
The native queue validates both 32-byte payloads, including cleanup fields.
ABI assertions pin both offsets and the combined payload size.
Malformed payloads cause no DMA publication or doorbell write.
The legacy diagnostic entry point still rejects combined KICK.

Mesa can append one final ordinary fragment after the paired partial render.
The whole triple must share the context, HWRT dataset, VM and target index.
Both fragments borrow the geometry job's single HWRT reference.
The final fragment has its own external waits, VM synchronization, command,
completion and signals. `pvr_context_fragment()` emits FRAG and UPDATE.
The partial render alone cannot complete that final signal.
Standalone fragments and longer fragment chains fail before publication.

KICK transport accepts zero or `CMD_EXECUTED`. Neither proves job completion.
CCCB consumption, the expected UFO value, fresh health and VM synchronization
must all succeed before results are published. CLEANUP keeps its strict
acknowledgement. Unknown completion retains backing objects and the batch.

The synchronous path returns `EDEADLK` for an unpublished self or forward
dependency that only a later job can satisfy. A later timeline signal can
satisfy an earlier point, but only the exact pair uses FENCE_PR ordering.
Published external fences retain normal waits and errors even if this batch
signals the same object again. Each batch permits at most `1 << 20` job and
sync visits, including empty jobs. Exhaustion returns `E2BIG`.
These are implementation limits, not firmware or UAPI limits.
The returned job count reports partial progress after an earlier completion.

## Native transfer submission

`TRANSFER_FRAG` uses the 176-byte stream decoder and transfer context sender.
The ioctl rejects unknown flags, nonzero HWRT fields, wrong context types
and malformed streams before execution. Job types retain all 32 UAPI bits;
unknown high values cannot alias a supported opcode.
External waits, retained context/VM references, PRE synchronization and
firmware completion remain required. Only successful completion performs
POST synchronization and publishes output fences. Failure marks the VM
uncertain and prevents premature backing-memory release.

Actual-source contracts cover mixed compute/transfer order, copy and
allocation errors, malformed admission, external fences, interruption,
timeout and backend loss. The physical optimal-image readback above exercises
this transfer path. It does not establish all formats or workloads.
