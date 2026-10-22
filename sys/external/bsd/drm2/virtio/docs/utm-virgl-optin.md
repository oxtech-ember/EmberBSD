# Experimental classic VirGL kernel

`EMBERVIRGL` includes `EMBERGPU` and adds the compile-time `VIRTGPU_VIRGL`
option. It requests only `VIRTIO_GPU_F_VIRGL` (bit 0) through the native
VirtIO feature negotiation. `EMBERGPU` continues requesting no GPU features;
the normal framebuffer/recovery configuration remains `EMBER64`.
EmberBSD owns this native attachment adaptation. The protocol feature number
comes from the retained Linux v5.6 `linux/virtio_gpu.h` import.

The attach message says “classic VirGL requested”, not negotiated or working.
The existing DRM feature report shows the negotiated result. A host that does
not offer VIRGL leaves the device on the 2D path. EDID, blob and context-init
features are not requested. No runtime switch or sysctl enables this option.

Build this separate kernel from a reviewed, committed source export using the
[cross-build workflow](../../../../../../tools/docs/cross-build.md), keeping a matched recovery bundle:

```sh
sh ember/build-kernel.sh /absolute/output EMBERVIRGL
```

The option does not bypass DMA eligibility, coherency, address translation,
bounce/IOMMU rejection, lease ownership, fences or reset guards. Those checks
may reject a host/device path. Do not relax them to obtain the first draw.
The experimental configuration retains the existing console and exclusive
VirtGPU child selection from `EMBERGPU`.

The production attach contract covers both requested masks, 32 host feature
combinations, nonmodern transport, initialization and queue failures. Removing
the bit request or adding EDID makes the contract fail. Run it with the existing
DMA, capset, context and submission contracts before a matched kernel build:

```sh
sh ember/tools/virtgpu-contract.sh
sh ember/tools/virtgpu-dma-contract.sh
sh ember/tools/virtgpu-capsets-contract.sh
sh ember/tools/virtgpu-context-contract.sh
sh ember/tools/virtgpu-submit-contract.sh
```

Configuration generation, focused GCC16 objects and software contracts are
separate from guest execution. The
[Mesa/libepoxy consumer](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/virgl-draw.md)
records bounded EGL/GLES shader, pixel and cleanup checks with the `virgl`
renderer and a verified ANGLE Metal host. The
[wlroots consumer](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/wlroots-virgl.md)
owns accelerated DRM-presentation, libseat and wscons enumeration evidence.
Those results require their matching host fixes and library providers.

Native modern PCI queue disable uses its supplied queue index after child
teardown clears the registry. Only activation requires a registered queue.
Run the focused regression with:

```sh
sh ember/tools/virtio-pci-queue-contract.sh
```

The production-path fixture covers both assertion modes. A failed host
scanout fence must return consumer failure and permit orderly teardown.
That boundary does not establish recovery of rendering after an arbitrary
in-flight 3D reset.

Application surfaces, physical input events, a complete native Wayland session,
live guest 3D reset, console recovery, physical GPU support and sustained
stability remain unaccepted. The [design's validation sequence](utm-virgl-design.md#validation-sequence)
remains the acceptance boundary.
