# Personal amd64 VirtGPU 2D trial

`GENERIC_VMGPU_TRIAL` is an opt-in source configuration for a disposable amd64
QEMU guest. It reuses this fork's native VirtGPU DRM/KMS driver. It does not
change GENERIC, INSTALL or the separate Intel personal-trial configuration.
Keep stock GENERIC as a distinct recovery kernel. No boot menu or image is
changed by this source configuration.

The foundation is OS `dc42c7ac341898fcbfc3b907f3795728ab5f4982`.
The selected device is modern PCI VirtIO GPU. QEMU 11.1.1 on macOS provides
`virtio-gpu-pci` and `virtio-vga` without host GL. The planned scenario uses
ordinary 2D resources, guest llvmpipe and real DRM dumb buffers/PRIME. It does
not request VirGL, blob resources or accelerated host rendering.

## Configuration contract

The trial includes GENERIC, removes framebuffer-only `viogpu`, and selects
`virtiodrm` at VirtIO. Module autoload is disabled. Its dependencies stay built
into the kernel. Do not load modules from another kernel build.

`VIRTGPU_VIRGL` stays off. The existing ARM64 eligibility gate is mandatory
for 3D backing and rejects other architectures. Ordinary 2D attachment does
not require that gate. Do not change or bypass it for this trial.

`VIRTGPU_CONSOLE` and `virtiodrmfb` also stay off. Their current console handoff
uses FDT simplefb. Use a serial console and retain the stock recovery path.
This first step does not qualify a VGA/UEFI console handoff, VT recovery or
visible scanout.

## Configuration regression

```sh
ruby ember/tests/amd64-vmgpu-config.rb /absolute/source \
    /absolute/fork-tools/bin/nbconfig /absolute/new-config-check
```

The test runs real config(1) for GENERIC and the trial. It checks selected
source files, actual attachments and generated option headers. To reproduce
the old configuration's missing native driver, supply `GENERIC` as the fourth
argument. This must fail after a successful configuration parse.

Build from a clean commit with the common GCC16 cross compiler. Use a separate
object directory and a private make wrapper. Existing fork host tools can be
read-only inputs. Set `NETBSDSRCDIR`, `NETBSDOBJDIR` and `BUILDINFO` to the actual
source, private object root and source commit. Start with the selected driver
objects and the normal kernel flags. A configuration pass is not a linked
kernel or boot result. No shared object, destination or module tree is an output.

## Required execution gates

After a complete kernel build and independent review:

1. Boot only a disposable VM through its separate trial-kernel choice.
2. Identify the selected kernel, PCI device and native DRM nodes. Check that
   no VirGL feature was negotiated.
3. Run real dumb-buffer, mmap, PRIME and lifetime probes. Check allocation
   failures and cleanup. Then run the common GBM/EGLImage pixel checks using
   llvmpipe and real primary-node buffers.
4. Check KWin 6.8 OpenGL virtual output with the actual DRM primary node selected
   through its upstream `KWIN_RENDER_NODES` input. Record renderer identity,
   successful image capture and errors. A software renderer needs no host GL.
5. Run classic `startplasma-wayland` with Xwayland, KSMServer, Activities, KDED,
   the default shell/panel, stable service owners and normal logout. Use an
   explicit CPU-rendered VM acceptance profile. Preserve the existing hardware
   renderer checks for their original targets.

KWin 6.8's virtual backend supports OpenGL only. `KWIN_COMPOSE=Q` cannot supply
a QPainter backend. Its no-DRM software device uses Linux `/dev/udmabuf`; that
allocator has no BSD implementation. `LIBGL_ALWAYS_SOFTWARE` alone therefore
cannot make the current framebuffer-only guest a full KWin session.

Source configuration, compilation, VM execution and physical Intel GPU
acceptance are separate results. None is established by the presence of this
configuration. No VM or device operation is part of the configuration regression.
