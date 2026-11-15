# GPU and display driver integration

Start with the [graphics architecture guide](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/graphics-and-compute.md)
for the render, display, and compute paths. This page maps the implementation interfaces for a new adapter.
Headers, source comments, and focused contracts remain authoritative; this index does not replace them.
Hardware applicability and acceptance belong to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).

## Select the adapter boundary

| Adapter | Interfaces to connect |
| --- | --- |
| Render or GPU compute | DRM device lifecycle, per-file state, GEM/VM, validated submission, completion, and the matching userspace UAPI |
| Display-only | DRM device lifecycle, buffer allocation/import as needed, KMS objects, atomic state, scanout lifetime, and display events |
| Combined GPU and display | Both paths, with explicit buffer ownership and synchronization between them |
| Dedicated accelerator | Its real execution ABI and runtime; do not expose KMS or generic model upload without implementing those contracts |

First identify an existing userspace driver and the kernel ABI it requires.
Preserve applicable upstream interfaces and licensing when adapting a driver.
A Linux driver also needs the native autoconfiguration, DMA, UVM, locking, and lifetime integration in this tree.
Use the existing native compatibility layer; add missing shared semantics there rather than copying them into each adapter.
An ioctl number or a successfully included Linux header does not establish the operation's semantics.

## Kernel interface map

Paths below are relative to this directory. Read the linked definitions before choosing callbacks.

| Area | Interface and implementation | Integration requirement |
| --- | --- | --- |
| Device lifecycle and capabilities | [`struct drm_driver`, feature flags, `drm_dev_*`](../../dist/include/drm/drm_drv.h), [core](../../dist/drm/drm_drv.c) | Initialize resources before registration. Implement file/device teardown and failure unwind. Enable render, modeset, atomic, and syncobj features only for implemented paths. |
| Discovery and identity | [Native PCI attachment](../../pci/drm_pci.c), [identity helpers](../../include/drm/drm_native_identity.h), [identity contract](drm-native-identity.md) | Use actual bus/platform identity. Primary and render minors are independent; never derive one by adding an offset to the other. |
| Build integration | [Common DRM selection](../files.drmkms), [PCI selection](../../pci/files.drmkms_pci), [native compatibility selection](../../linux/files.drmkms_linux) | Connect the adapter to its bus/platform `files.*` and kernel/module dependencies; retain the intended built-in or module configuration. |
| Userspace requests | [`drm_ioctl_desc` and dispatch flags](../../dist/include/drm/drm_ioctl.h), [DRM UAPI](../../dist/include/uapi/drm/drm.h) | Define queries and operations that the userspace driver consumes. Validate sizes, handles, offsets, flags, authorization, and device capabilities before device access. |
| GEM and native CPU mappings | [`drm_gem_object`](../../dist/include/drm/drm_gem.h), [GEM core](../../dist/drm/drm_gem.c), native `mmap_object` / `gem_uvm_ops` in [`drm_driver`](../../dist/include/drm/drm_drv.h) | Choose backing storage and mapping policy. Retain objects across handles, UVM mappings, GPU mappings, and queued work; release each owner on its own completion path. |
| Memory backend | [Native CMA helper](../../include/drm/drm_gem_cma_helper.h), [TTM interface](../../dist/include/drm/ttm/ttm_bo_api.h) | Select the helper that fits the hardware's address and memory constraints. A GPU page table, an IOMMU map, and a CPU mapping are separate resources. |
| Sharing | [PRIME implementation](../../dist/drm/drm_prime.c), [CMA import](drm-cma-import.md), [PRIME mmap](drm-prime-mmap.md) | Implement the supported export/import/mmap directions. Handle foreign buffers and failure unwind explicitly; self-import does not prove foreign import. |
| Execution and dependencies | [Scheduler interfaces](../../dist/include/drm/gpu_scheduler.h), [native batch API](../../include/drm/drm_sched_batch.h), [submission contract](drm-submit.md) | Reuse the applicable scheduler path. The native synchronous batch API uses the existing scheduler; it is not required for every asynchronous driver. Supply device execution, retirement, and isolation/quarantine behavior. |
| Fences and sync objects | [Fence interface](../../include/linux/dma-fence.h), [reservation interface](../../include/linux/dma-resv.h), [syncobj contract](drm-syncobj.md), [DMA-BUF contract](drm-dmabuf-sync.md) | Capture dependencies before publishing outputs. Publish pending work before device access and signal real completion/error. Keep references valid through waits, callbacks, close, and cancellation. |
| KMS objects | [Mode configuration](../../dist/include/drm/drm_mode_config.h), [planes](../../dist/include/drm/drm_plane.h), [CRTCs](../../dist/include/drm/drm_crtc.h), [encoders](../../dist/include/drm/drm_encoder.h), [connectors](../../dist/include/drm/drm_connector.h) | Model the real display pipeline. Report supported formats/modifiers and modes; keep hardware-specific validation in the adapter. |
| Atomic display state | [Helper callbacks](../../dist/include/drm/drm_modeset_helper_vtables.h), [atomic helpers](../../dist/include/drm/drm_atomic_helper.h), [GEM framebuffer contract](drm-gem-framebuffer.md) | Connect state checking and commit, retain the displayed buffer, and complete events at the actual hardware boundary. Reuse common helpers where they fit. |
| Display events | [Vblank interface](../../dist/include/drm/drm_vblank.h), [hotplug contract](drm-hotplug-events.md) | Wire actual interrupts or supported polling. Hotplug advises userspace to re-query; it neither detects the connector for the driver nor grants access. |

The [DRM mode UAPI](../../dist/include/uapi/drm/drm_mode.h) and
[format/modifier definitions](../../dist/include/uapi/drm/drm_fourcc.h) describe the public display interface.
Do not infer supported operations from the presence of these definitions.

## Memory, completion, and teardown

Use the focused contracts for ownership rules, error semantics, and executable checks:

- [Submission](drm-submit.md): admission, captured dependencies, output publication, cancellation, drain, and retained hardware resources.
- [Sync objects](drm-syncobj.md) and [fence chains](../../linux/docs/dma-fence-chain.md): waits, timeline payloads, completion errors, and callback lifetime.
- [DMA-BUF synchronization](drm-dmabuf-sync.md): reservation ordering, fence arrays, descriptor rights, and sync-file import/export.
- [CMA import](drm-cma-import.md), [PRIME mmap](drm-prime-mmap.md), and [GEM framebuffers](drm-gem-framebuffer.md): mapping authorization and balanced ownership.
- [Native identity](drm-native-identity.md) and [hotplug events](drm-hotplug-events.md): registration, reader/event lifetime, and matching Ports consumers.

These contracts do not make every memory backend interchangeable.
The native CMA import helper requires one contiguous DMA segment.
DMA-BUF sync-file import/export does not implement `DMA_BUF_IOCTL_SYNC` or CPU cache maintenance.
An error fence is not proof that the device stopped DMA. Close admission and drain software users,
then prove hardware retirement or isolation before freeing device-visible resources.
Document unsupported reset, suspend/resume, detach, and recovery paths explicitly.

## Read an existing adapter

These are implementation examples, not a device support list or templates to copy unchanged.

| Example | What to inspect |
| --- | --- |
| [Native V3D](../../bcmv3d/bcmv3d_drv.c) and [contracts](../../bcmv3d/docs) | Driver registration, ioctl permissions, GEM/UVM, and separate render/display integration. Its absent foreign-import SG hook is a meaningful limit. |
| [Native PowerVR](../../powervr/pvr_drv.c) and [execution contract](../../../../../arch/arm/sunxi/docs/a733-pvr-execution.md) | Render-only DRM device, userspace ABI, firmware bridge, shared batch scheduler, VM ownership, and failure quarantine. Firmware completion rules remain device-specific. |
| [Native display adapter](../../../../../arch/arm/sunxi/sun60i_a733_drm.c) | KMS-only capabilities, CMA-backed buffers, and atomic callbacks without a rendering interface. Its fixed-mode constraints are not a general KMS requirement. |
| [Native VirtGPU](../../virtio/README.md) | Guest/host resource lifetime, transport completion, native mappings, and explicitly disabled protocol features. |

For a dedicated accelerator, the [native NPU task contract](../../../../../arch/arm/sunxi/docs/a733-npu-tiny-task.md)
illustrates a bounded trusted operation. It does not define a general tensor, model, or arbitrary-command UAPI.
Keep model preparation and numerical reference data in the runtime or its tests.

## Connect userspace

Kernel and userspace must agree on device identity, queries, object layouts, memory operations, submission, and synchronization.
Use the exact UAPI consumed by the selected backend. In particular, vendor PowerVR Services and upstream PowerVR DRM are different interfaces.
Keep userspace integration in the shared Ports recipes:

| Component | Owner reference | Work for the new adapter |
| --- | --- | --- |
| libdrm | [Recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/x11/libdrm) | Native device discovery and ABI wrappers where the consumer uses them; match the kernel identity contract. |
| Mesa | [Recipe and patches](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/graphics/MesaLib) | Enable the matching backend, native OS integration, and required compiler dependencies in the common build. |
| Gallium backend | Upstream [`pipe_screen`](https://docs.mesa3d.org/gallium/screen.html) and [`pipe_context`](https://docs.mesa3d.org/gallium/context.html) | Screen capabilities/resources and rendering/compute context operations. Reuse a matching backend or add its hardware implementation. |
| Shader compilation | Upstream [NIR](https://docs.mesa3d.org/nir/index.html) and the selected Mesa driver | Keep shader translation and optimization in userspace. Reuse shared compiler passes and the GPU's backend. |
| Vulkan ICD | [Loader recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/graphics/vulkan-loader), [Mesa selection](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/graphics/MesaLib/Makefile) | Implement the supported Vulkan device/queue/resource operations in the ICD, with the matching kernel backend. Install its discovery metadata and check loader enumeration separately from execution. |
| Cross-build generators | [Mesa generator contract](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-generators.md) | Run native generators on the host and build libraries/drivers for the target through the shared recipe. |
| Desktop and presentation | [Graphics integration](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-graphics), [device/input integration](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/drm-input.md) | Join GBM/EGL or Vulkan WSI, compositor, device discovery, seat access, and input as required by the selected session. |

If an existing Vulkan driver supplies the needed features, [Zink](https://docs.mesa3d.org/drivers/zink.html) may provide OpenGL without another hardware Gallium backend.
That choice still needs its buffer-sharing and presentation integration.

## Validate the selected path

Run the focused checks linked above for the changed mechanisms, then build the affected kernel/modules and Ports consumers.
Use the selected SDK/Ports toolchain and the existing owner procedures; exact commands stay in those contracts.

| Result to establish | Required evidence |
| --- | --- |
| Safe kernel interface | Rejected malformed requests, authorization, allocation failure/unwind, dependency errors, close with pending work, and supported teardown/recovery behavior |
| Render or compute execution | Expected pixels or numerical output, real completion, cache visibility, repeated resource reuse, and the intended hardware backend |
| Shared-buffer path | Intended exporter/importer pair, formats/modifiers, bounds, synchronization, and balanced release |
| Direct display | Modeset, actual scanout/page-flip completion, buffer retirement, and supported hotplug/session transitions |
| Accelerated UI | Selected application and compositor renderers, physical presentation, frame pacing, copies, and memory use for the intended workload |

Reuse [Mesa source checks](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/graphics/MesaLib/tests/README.md),
the [UI measurement procedure](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/ui-performance.md),
and the [Qt/KWin application example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/kwin-frame-check) where applicable.
Enumeration, a build, an offscreen frame, a nested compositor, and a physical desktop establish different results.
Record exact hardware applicability in DevBoards and keep source contracts with their implementation.
