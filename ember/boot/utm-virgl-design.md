# VirtIO-GPU acceleration for EmberBSD in UTM

Status: native 2D, four offscreen guest GLES lifecycles and four accelerated DRM frames verified; the separate [classic VirGL opt-in](utm-virgl-optin.md) still needs application surfaces, complete Wayland-session and recovery acceptance.
Reviewed on 2026-10-06 against EmberBSD `b1d21397dca` and UTM 4.7.5.
Target: the current NetBSD 11/aarch64-based EmberBSD installation.

On 2026-10-06, `67a611acb9b` booted with the native DRM child. PCI discovery,
32 cross-process GEM/PRIME mapping lifetimes and visible 800x600 KMS color
bars passed. A native labwc/Pixman session displayed Kate without Xorg.
Physical pointer motion, clicks, dragging, typing and saving a file passed
with the Ports absolute-input adaptation. Console repair source, native kernel
builds and contract tests also pass; visible VT handoff and exit/crash recovery
still need runtime acceptance. These results do not establish GPU rendering.
Rapid UI injection is not a reliable physical-input acceptance test. Reproducible
third-party recipes and patches live in
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/wayland-utm),
and runtime probes live in
[EmberBSD Examples](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/wayland-utm).

## Outcome and scope

Make a native Wayland session the primary GPU integration target on the
current EmberBSD. Start with labwc/wlroots to test the platform independently
of GNOME-specific porting. Rendering must execute on the Mac GPU.
The path is Mesa VirGL, the standard VirtGPU DRM ABI, VirtIO, UTM's
virglrenderer, and an accelerated ANGLE backend. Keep one display initially.
Preserve the working framebuffer kernel, display configuration and login.

The former GNOME/Xorg demonstration VM has been removed; its acceptance is
historical, not an available recovery session. Preserve matched framebuffer
kernels and use isolated test roots. Native GNOME Wayland remains a later
integration result; the earlier Mutter build disabled its native backend.
A successful labwc test does not complete GNOME Wayland support. Physical-board GPUs,
Vulkan/Venus, compute, multi-head and live driver unloading remain outside
the first result. A nested Wayland client does not establish KMS or GPU support.

## Evidence that changes the first proposal

Linux introduced VirtIO KMS before VirGL 3D. Its established stack separates
kernel buffer/display management from Mesa rendering and desktop integration
([Mesa history](https://docs.mesa3d.org/drivers/virgl.html)). Adopt that order.

FreeBSD [PR 499](https://github.com/freebsd/drm-kmod/pull/499) was closed,
unmerged, in favour of [PR 517](https://github.com/freebsd/drm-kmod/pull/517).
The replacement is still open on the review date. It moves VirtIO compatibility
out of the GPU module after maintainer review. Its author reports UTM/aarch64
VirGL tests; an independent tester reports accelerated Xorg and sway on
amd64/QEMU. These are external results, not EmberBSD validation.

OpenBSD's [viogpu](https://man.openbsd.org/viogpu.4) is a wscons console driver.
Adding framebuffer mmap/damage updates to that design does not supply the
VirtGPU rendering ABI. Keep it as a console reference, not the 3D foundation.

Android's [gfxstream](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/main/README.md)
uses a different graphics protocol. [UTM 4.7.5](https://github.com/utmapp/UTM/blob/v4.7.5/Documentation/Graphics.md)
marks gfxstream, Venus and MoltenVK as future work. Do not base this port on
features documented only for newer upstream QEMU or another VMM.

## Initial gaps and baseline audit

The saved recovery VM configuration uses virtio-ramfb with GL off, genfb/wsfb and llvmpipe.
The console driver has no DRM interface; opening the existing DRM device
nodes returns ENODEV. Installed Mesa 21.3.7 lacks the VirGL DRI driver.

The initial `dist/drm/virtio` sources were not built or attached. They required
missing Linux VirtIO and GEM shmem interfaces. NetBSD already has UVM-backed
GEM allocation, page pinning and a native mmap fault path; reuse that base.

Do not assume the imported driver is an exact Linux release. After removing
NetBSD metadata and blank lines, `virtgpu_drv.c` and `virtgpu_vq.c` match
Linux v5.6, while `virtgpu_object.c` differs in resource-ID allocation and
cached-map initialization. Audit the full file set and relevant upstream
fixes before enabling it. Record the selected revision, local delta and
backports; a wholesale DRM upgrade is not a prerequisite assumed by this plan.

## Component boundaries

### VirtIO compatibility and attachment

Put the minimum Linux VirtIO API required by this driver beside the existing
`drm2/linux` compatibility code, with headers under `drm2/include/linux`.
Use explicitly prefixed native types at the boundary to avoid collisions
with NetBSD's `struct virtqueue`. Keep DRM, capsets and rendering out of this
transport layer. Do not build a general bus framework beyond this first user.

Adapt to NetBSD autoconfiguration and `dev/pci/virtio.c`; do not copy FreeBSD
newbus, DMA assumptions or platform workarounds. Reuse native feature
negotiation, queue allocation, bus_dma and interrupt handling. Gate ownership
so viogpu and the DRM child never attach to the same device. Initialize DRM
before registering its nodes; leave the ordinary EMBER64 configuration intact.

Define contracts for readable/writable SG boundaries, errno conversion,
interrupt rearming, queue backpressure and reset/callback exclusion.
NetBSD's `virtio_enqueue_reserve()` asserts `nsegs <= vq_num` even when indirect
descriptors exist. Bound or reshape backing-list submissions accordingly;
an oversized request must fail rather than wait forever for impossible space.
Test any shared transport change against existing VirtIO consumers.

### GEM, UVM and DMA

Implement the missing shmem contract around NetBSD's GEM/UVM ownership and
`gem_uvm_ops`. Keep pages pinned while the host may access them. Distinguish
queue completion from GPU fence completion and resource detach/unref lifetime.
Do not release mappings merely because a command entered the used ring.

Specify CPU/device synchronization for commands, responses, backing pages and
readback through NetBSD's DMA APIs. Do not equate physical and DMA addresses
or discard cache maintenance based on a FreeBSD-only implementation detail.
Exercise mmap faults, fork/close, failed submissions and interrupted clients.

Preserve every imported license and identifier. The missing Linux shmem
helper is GPL-2.0; importing it needs an explicit, correctly placed licensed
component. Do not copy it into a BSD-labelled subtree or label adaptations
permissive. Prefer existing native primitives where they provide the contract.

### DRM/KMS and classic VirGL

Keep the standard VirtGPU ioctl ABI used by Mesa. First implement dumb
buffers, scanout, flush, cursor and page-flip completion with 3D disabled.
Then enable contexts, capsets, 3D resources/transfers, EXECBUFFER and fences.
Only advertise implemented capabilities; blob, host-visible and context-init
features stay disabled until separately implemented and tested.

Support same-device PRIME export/import and cross-process descriptor lifetime
for desktop buffer sharing. Reject unsupported cross-device imports clearly;
that restriction must not break same-device round trips. Primary-node KMS
and render-node permissions remain distinct. Do not allow CREATE_DUMB on a
render node simply to make a software compositor start.

### Mesa, Wayland and session integration

First prove visible KMS output, then run labwc directly on DRM without an
Xorg parent. Use its software renderer to isolate KMS/input failures. Next
prove EGL/GBM render/readback with VirGL and switch labwc to GLES/VirGL.
Test native clients and client/compositor dma-buf sharing before Xwayland.

pkgsrc 2026Q2 already has labwc 0.9.7, wlroots 0.19.3, seatd 0.9.3 and
libopeninput recipes. The latter implements the libinput interface with
wscons/kqueue; Linux evdev is not a prerequisite for this path. The selected
binary repository has seatd/libopeninput but no labwc/wlroots compositor
packages in its current index. Build the missing packages reproducibly.

Use the packaged NetBSD seat/VT adaptations and launch the compositor as the
existing user. Verify device handoff, keyboard modifiers, absolute pointer
coordinates, VT release/reacquire and text-console recovery on compositor exit
or crash. A nested test receives input through Xorg and cannot validate this.
Keep XDM for the saved X11 session; an XDM .xsession is not a native Wayland
launcher. Introduce a separate console/session entry for Wayland testing.

Select current supported common dependencies and carry compatibility fixes
in Ports. Its Mesa 26.2.4 source recipe selects classic VirGL, softpipe and
llvmpipe with common LLVM 23.1.2/GCC 16.2, EGL/GBM, GLES, X11 and Wayland.
ORC JIT is our selected current LLVM path, not an AArch64 upstream requirement.
The complete package, software/JIT runtime and installed libepoxy consumer
pass on AArch64. The [GBM DMA-BUF consumer](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/gbm.md)
also passes with native VirtGPU 2D and llvmpipe. Current wlroots/labwc and
the accelerated session still need acceptance. Existing Mesa 21 software
results apply only to the temporary recovery/comparison stack.
Remove that prefix after current Mesa and rebuilt wlroots/labwc pass the same
lifecycle and native-input/session checks. Do not keep older per-application
LLVM or disable llvmpipe to bypass compatibility failures.

Stage the common stack privately before any package replacement. Mesa 26 no
longer installs shared libglapi and instead uses a versioned libgallium DSO.
Audit actual EGL/GLX/GBM, Qt/GNOME/Xorg and compositor dependencies; rebuild
consumers instead of creating fake SONAME links. Preserve the matched libdrm
and input-library selection. Full pixel readback, load/unload, JIT and native
session checks gate promotion. Remove software overrides only in the explicit
GPU test session.

Treat accelerated Xorg modesetting/glamor/DRI3 as an optional compatibility
test, not a prerequisite for native Wayland. Porting GNOME additionally needs
Mutter's native backend, device/session integration and compatible Shell
dependencies. Installed Mutter already links EGL; its pkgsrc recipe disables
Wayland/native unconditionally, but disables EGL only when unavailable.
Mutter 40.2's native build requires libsystemd or libelogind as well as
udev, libinput and GBM; the labwc seatd launcher does not satisfy that interface.
Resolve this in the GNOME port instead of treating it as a configure toggle.

### Native console restoration

The first native session exits cleanly but leaves the display inactive. The
firmware genfb console remains attached to the old ramfb; VirtGPU has no DRM
console framebuffer. Native generic drm_client framebuffer helpers are stubs.
Do not attach a second final wsdisplay console or rely on genfb flag toggles
to migrate the existing console.

Use a driver-owned framebuffer helper and native drmfb child. Reserve the
selected firmware display before its final simplefb attachment, only in the
experimental configuration; retain early output and a pre-takeover failure
fallback. Keep console pages and the DMA mapping alive across user sessions.
A CPU shadow and a separate upload worker isolate rasops from sleeping GPU
commands. Coalesce damage and fence TRANSFER, SET_SCANOUT and FLUSH; never
upload or restore console scanout while userland owns graphics mode.

Restore on native wsdisplay mode changes and lastclose, including redraws
after VT reacquisition and compositor death. This is ordinary console
recovery; panic/DDB output requires a separate bounded polling path and is
not promised. Preserve the original kernel and SSH/serial recovery.

## Validation sequence

Each stage produces a small reproducible result before the next is enabled.
New contract probes use C and shell; upstream tools retain their real build
dependencies. Record source revisions, flags, hashes and host display settings.

| Stage | Required evidence |
|---|---|
| Host preflight | Record the actual GL-capable UTM device and ANGLE backend. Where available, verify a known Linux guest on the same UTM build; an external FreeBSD report is a reference only. |
| Build and ownership | Clean native kernel build, one GPU child, DRM initialization, feature mask and reachable nodes. Boot with both GL and non-GL devices. |
| Memory and 2D | Create/map/draw/scan out a known pattern, repeated flips and cursor updates; test a buffer whose backing list exceeds direct-ring capacity. No 3D claim at this stage. |
| Native input/session | labwc on DRM with software rendering, seat/VT handoff, wscons keyboard and pointer, orderly exit and crash recovery. No Xorg parent. |
| VirGL | Query real capsets, create contexts, render a known image, wait for its fence and verify pixels. Run a visible EGL/GBM test such as kmscube. |
| Wayland and sharing | labwc GLES/VirGL, same-device PRIME/dma-buf sharing between native clients and compositor, resize, redraw and a saved file; then one legacy app through Xwayland. |
| GNOME integration | Separate native-Mutter build and session checks before claiming GNOME Wayland. Preserve the working Xorg desktop meanwhile; test login and cold boot for any promoted default. |
| Reliability | Thirty-minute rendering/allocation workload; kill clients with submissions in flight; malformed sizes/handles/pointers; queue exhaustion and injected attach failures. Check memory, completion counters and kernel diagnostics. |
| Recovery | Restore both the saved UTM display configuration and framebuffer kernel/session. Confirm visible login and input. A kernel-only rollback is insufficient if the virtual device changed. |

Add a regression for each failure found. Validate the normal framebuffer
configuration after changes to shared VirtIO or DRM compatibility code.
Keep diagnostics for pending requests, fences and live buffer objects.
Reject detach while clients or console mappings remain; hot removal and live
unloading are not promised by the initial supported configuration.

Acceptance requires correct images, a VirGL renderer backed by the Mac GPU,
the expected loaded libraries, and a native Wayland session after reboot. A Metal
display window, `direct rendering: Yes`, a device node or high FPS alone
does not prove guest GPU acceleration. UTM also uses Metal to present CPU frames.
Compare frame time and CPU use against the saved llvmpipe case at the same
resolution/workload; report measurements without inventing a speed target.

## Alternatives and decision

Retaining wsfb is the recovery path. Extending console mmap would help 2D but
leave the rendering ABI missing. Forwarding GL to a separate host service
would add a different deployment path. Importing FreeBSD's Linux 6.13 stack
wholesale would enlarge this task into a DRM upgrade. None is the first choice.

Proceed with a narrow VirtIO compatibility layer, NetBSD-native memory glue,
the audited existing VirtGPU core, then staged KMS, native input, VirGL and
Wayland integration. This replaces the module-local shim and Xorg-first
proposals. Wayland avoids requiring an accelerated X server for native clients;
it does not remove DRM, buffer synchronization or input-porting work, and no
performance gain is claimed without measurements. The validation sequence
remains the acceptance contract for the experimental implementation.

## Pinned references

- [FreeBSD v2 source a62ec6b](https://github.com/borovikovd/drm-kmod/tree/a62ec6b1674bbcd4cf301fbd5cd575b8c00d2e90); [VirtIO adapter](https://github.com/borovikovd/drm-kmod/blob/a62ec6b1674bbcd4cf301fbd5cd575b8c00d2e90/drivers/virtio/linuxkpi_virtio.c).
- [Independent FreeBSD test, revision 83e5f9dc19](https://github.com/freebsd/drm-kmod/pull/517#issuecomment-5974708741); this predates the reviewed head.
- [Linux v5.6 VirtGPU](https://github.com/torvalds/linux/tree/v5.6/drivers/gpu/drm/virtio); [shmem helper](https://github.com/torvalds/linux/blob/v5.6/drivers/gpu/drm/drm_gem_shmem_helper.c).
- [DRM buffer sharing and lifetime](https://www.kernel.org/doc/html/latest/gpu/drm-mm.html#prime-buffer-sharing).
- [QEMU GPU backends](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html); latest QEMU features are not assumed available in UTM 4.7.5.
- [pkgsrc labwc](https://github.com/NetBSD/pkgsrc/tree/pkgsrc-2026Q2/wayland/labwc), [wlroots](https://github.com/NetBSD/pkgsrc/tree/pkgsrc-2026Q2/wayland/wlroots), [libopeninput](https://github.com/NetBSD/pkgsrc/tree/pkgsrc-2026Q2/devel/libopeninput), [seatd](https://github.com/NetBSD/pkgsrc/tree/pkgsrc-2026Q2/sysutils/seatd).
- [Mutter package](https://github.com/NetBSD/pkgsrc/blob/pkgsrc-2026Q2/wm/mutter/Makefile); [reported NetBSD labwc use on Intel](https://mail-index.netbsd.org/pkgsrc-users/2026/07/05/msg043169.html), not an aarch64/UTM result.
- [Wayland architecture](https://wayland.freedesktop.org/architecture.html).
