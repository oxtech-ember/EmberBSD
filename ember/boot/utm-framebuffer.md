# UTM firmware framebuffer on AArch64

UTM 4.7.5 (QEMU, Apple Silicon) can run `EMBER64` with Hypervisor.framework,
the `virt` machine, and `virtio-ramfb`. Use the firmware framebuffer through
`genfb` and Xorg's `wsfb` driver. This is software rendering, not a GPU
acceleration claim.

`EMBER64` already excludes the VMware backdoor attachment that prevents
this configuration from booting with HVF. A NetBSD GENERIC64 fallback
kernel may require switching the VM back to TCG.

## Boot configuration

In the EFI partition's `boot.cfg`, use:

```text
menu=EmberBSD:userconf disable viogpu;boot netbsd
menu=EmberBSD single user:userconf disable viogpu;boot netbsd -s
menu=Boot prompt:prompt
default=1
timeout=5
```

The NetBSD 11 `viogpu` driver does not implement the framebuffer `mmap`
needed by `wsfb`. Disabling only this child used to leave a second problem:
`virtio_pci_attach` reset the GPU before looking for a child. UTM's GPU reset
cleared the console surface. The kernel and Xorg could still access the
firmware memory, while UTM displayed “Display output is not active.”

Commit `b4f718dabd085ed117a24f84d8558e4a43091dc0` moves reset, ACK and DRIVER
status into the matched-child path of `virtio_pci_rescan`. With `viogpu`
disabled, the PCI transport preserves firmware state. Other matched PCI
devices still initialize before their child attaches.

Keep the original kernel and its module directory as a separate recovery
set. Build and install the EmberBSD kernel and modules together using
`ember/build-kernel.sh`; never mix the stock module directory into that set.

## Regression checks

Run `sh ember/tools/virtio-pci-rescan-test.sh` from a source checkout. It
checks that initial attach cannot reset or claim an unselected device,
then executes the actual rescan function against a mock bus. The cases are
an unavailable child, a child becoming available, and an attached child.
This does not test recovery from a real failed child attachment.

The previous source and a mutation reintroducing the unconditional reset
in initial attach must fail. The corrected source must pass. A native
`EMBER64` build and the existing builder contracts remain required.

The integration regression needs a real UTM window:

1. Use 4 vCPUs, 4 GiB RAM, HVF, `virtio-ramfb`, USB keyboard and pointer,
   virtio disk and network, and the boot configuration above.
2. Confirm `genfb0` and `wsdisplay0` attach, with `viogpu` disabled.
3. Start Xorg with `Driver "wsfb"`; verify a visible window and input.
   `xdpyinfo` succeeding alone does not detect the original defect.
4. Check disk writes, DHCP, DNS and HTTPS to cover other PCI children.
5. Reboot and repeat the visible display and service checks.

The October 2026 test used NetBSD 11.0/aarch64 userland and UTM's bundled
EDK2 firmware, SHA256
`ee769c4bf42a5350d33345fc1b16d00156fe0c25fb3c68debd003bd844e4e3fa`.
Its RAM framebuffer mode was 800×600. The patched `EMBER64` kernel was
`fe0fe339059cc1d820c381a8553904e4163a33af8d3cc7fa25348ae75eae3b82`.
KDM and a KDE 4 session rendered after reboot; PCI disk and network worked.
This establishes neither physical-board graphics support nor 3D support.

See [NetBSD's QEMU ARM guide](https://wiki.netbsd.org/ports/evbarm/qemu_arm/),
[UEFI wsfb notes](https://wiki.netbsd.org/tutorials/x11/how_to_use_wsfb_uefi_bios_framebuffer/),
and the [standalone desktop example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/kde-utm).
