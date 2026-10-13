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
[cross-build workflow](cross-build.md), keeping a matched recovery bundle:

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
checked separately from guest execution. On 2026-10-08, the kernel built from
`103bcbdc2144c5667398ce2de3d65f9759db36e9` negotiated VirGL and two capsets.
The [installed Mesa26/libepoxy consumer](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/virgl-draw.md)
passed four EGL 1.5/GLES 3.0 shader/pixel/cleanup lifecycles with renderer
`virgl`; the paired host proved ANGLE Metal on Apple M3. Target and QEMU exited
zero, with unchanged input filesystem and verified live library providers.
The subsequent matched kernel from
`9c0b92bea0af2b69fbdfcbf633b7c0be74be650c` also passes four
[accelerated wlroots DRM presentations](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/wlroots-virgl.md)
at 1280x800, including GLES2 pixel checks, matching presentation events, an
active libseat session and enumeration of two actual wscons devices. The
paired host must include the Ports Cocoa context fix; both target and QEMU
exit zero with the recorded package and host providers.

That kernel commit fixes native modern PCI queue disable after child teardown
has cleared the queue registry. The disable operation uses its supplied index;
only activation needs the registered queue. Run its focused regression with:

```sh
sh ember/tools/virtio-pci-queue-contract.sh
```

The extracted production paths pass 192 cases in each of two assertion modes,
including native execution in the guest. The same new kernel with the old
host still fails the scanout fence, but now returns consumer failure and halts
cleanly without the original panic. This verifies that observed teardown path,
not recovery of rendering after arbitrary in-flight 3D reset.

Application surfaces, physical input events, a complete native Wayland session,
live guest 3D reset, console recovery, physical GPU support and sustained
stability remain unaccepted. The [design's validation sequence](utm-virgl-design.md#validation-sequence)
remains the acceptance boundary.
