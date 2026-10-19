# Native DRM PCI identity

Experimental native VirtGPU and PCI DRM drivers with a Linux `pci_dev`
publish immutable, read-only PCI metadata at `hw.drm2.identity<minor>`.
A primary or render minor addresses the same
record. Readers need no DRM master, primary-node open, `/dev/pci` or
`/dev/drvctl` access. This exposes public identity only and grants no KMS,
PCI configuration, buffer-allocation or rendering authority.

The native VirtIO child has no Linux `pci_dev`. Its existing bus-ID attach
path identifies the actual PCI parent, segment, bus and transport locators.
It snapshots ID, class/revision and subsystem-ID registers once at attach.
Non-PCI transport attachment does not fabricate PCI metadata.

PCI DRM drivers with a Linux `pci_dev` attach through `drm_pci_attach()`.
These include i915, amdgpu, radeon, nouveau, vmwgfx and via.
After its last failure return, `drm_pci_attach()` fills the record from the
shim's attach arguments and cached IDs. The segment is the domain.
The record always sets the revision flag; the shim reads the revision at attach.
`drm_dev_init()` clears the record first, and `drm_dev_register()` copies it.
Other drivers which do not fill the cache retain their established discovery
behavior. A metadata registration failure now also fails `drm_dev_register()`
for these PCI drivers.

## Version 1 record

`drm_native_identity.h` defines a fixed 68-byte structure: seventeen native
endian `uint32_t` words, without pointers, strings, `dev_t` or implicit ABI
padding. This is a native sysctl interface, not a Linux DRM ioctl.

| Word | Field | Meaning |
|---|---|---|
| 0 | version | 1 |
| 1 | length | 68 bytes |
| 2 | flags | 1: primary present; 2: render present; 4: revision valid |
| 3 | bus_type | 1: PCI |
| 4–7 | domain, bus, device, function | Actual PCI address; segment is the domain |
| 8–11 | vendor, product, subvendor, subproduct | Actual 16-bit PCI IDs |
| 12 | revision | 8-bit PCI revision, meaningful with flag 4 |
| 13–14 | primary_major, primary_minor | Registered primary character device |
| 15–16 | render_major, render_minor | Registered render character device, with flag 2 |

Unknown flags, versions and lengths must be rejected. Absent fields are zero.
Primary and render indices have independent allocators. Consumers match the
record's major/minor to `fstat` or `stat`, and verify any returned pathname's
character-device identity. They must not derive a render index from a card
index. This record describes the currently registered cdev, not a persistent
identity for an old fd after device removal and reuse of the same rdev.

## Publication and lifetime

DRM registration publishes the record after both minors and the optional
legacy load callback succeed, before modeset publication. Metadata failure
fails registration, tears down every partially created leaf, unloads a
successfully initialized legacy driver and unregisters the minors. Metadata
storage owns only a copy of the cached identity, never PCI/device pointers.

Unregister removes both leaves before unloading the driver or unregistering
its minors. Native `sysctl_teardown` acquires the tree write lock; ordinary
`sysctl_lookup` holds its read lock during the copy. The record is freed only
after teardown returns, so concurrent reads cannot dereference freed device
storage. The shared permanent `hw.drm2` parent remains owned by the DRM core.
The VirtGPU experimental attachment still deliberately rejects hot detach;
the common DRM unregister contract is covered independently.

The matching Ports libdrm probe preserves its public structures, existing
PCI-address folding and discovery for drivers without metadata. Only absent
leaves use the legacy path. It never converts malformed metadata to a fake
identity. A zero public flags argument still returns vendor/product/subsystem
IDs; `DRM_DEVICE_GET_PCI_REVISION` controls only revision retrieval.

## Descriptor validation and checks

Native `sync_file_get_fence` now checks `DTYPE_MISC` and exact sync-file
fileops while holding `fd_getfile`'s reference, before reading `f_data`.
It acquires the fence reference before releasing the file. Regular files,
sockets, other miscellaneous files and closed descriptors return NULL;
the existing VirtGPU EXEC caller converts that to `EINVAL`.

```sh
sh ember/tools/drm-native-identity-contract.sh
sh ember/tools/drm-pci-identity-contract.sh
sh ember/tools/virtgpu-contract.sh
sh ember/tools/virtgpu-console-contract.sh
sh ember/tools/virtio-transport-contract.sh
```

The new host contract compiles actual registration, unregister, PCI snapshot
and sync-fd functions. It checks partial failures, absent cache, independent
indices, immutable storage, a concurrent reader, node reuse and foreign fds.
Mocks represent sysctl/autoconf/file boundaries, not replacements for the
production algorithms. The PCI shim contract runs the actual
`drm_pci_native_identity()` and checks every record field.
It also requires one call after the last failure return of `drm_pci_attach()`. Host checks do not establish native kernel compilation
or hardware concurrency behavior.

Native `-Werror` compilation of the four changed translation units and the
new contracts passes on NetBSD 11/aarch64. This does not link a kernel: the
changed `drm_device` layout requires rebuilding every dependent object for
both complete kernel configurations. Runtime acceptance is still pending.
A GCC16 cross-built Jasper Lake trial kernel with the `drm_pci.c` change boots;
its sysctl record and libdrm lookup have no hardware check yet.
Live acceptance
must use the matched libdrm for `drmGetDevices2`, `drmGetDevice2(render_fd)`,
render-name and node-type lookup, with no primary master and no global PCI
access. Retest negative KMS permission checks. These changes do not negotiate
VIRGL, alter the classic VirtGPU ABI, relax CREATE_DUMB or promote 3D support.

## Platform devices

Native V3D also publishes `hw.drm2.platform<minor>`. Its separate version-1
record is 160 bytes: version, length, flags, bus type (2), primary major/minor,
render major/minor, then two NUL-terminated 64-byte strings. `fullname` names
the actual autoconf instance; `compatible` is `acpi:BCM2850`, the ACPI ID
accepted by the BCM2712 attachment. This does not fabricate a device tree.
Flags 1 and 2 describe actual registered nodes. The PCI record is unchanged.

Both leaves contain the same immutable snapshot. Platform registration uses
the same unwind, reader-drain and unregister ordering as PCI. The host
contract covers both schemas, independent minor indices, all three sysctl
allocation failures and concurrent teardown. Matching Ports libdrm maps this
record to `DRM_BUS_PLATFORM`; malformed or inaccessible metadata cannot fall
back to legacy discovery. Physical CM5 enumeration, render-node folding and
automatic Mesa V3D selection passed on 2026-10-11; see the
[installed Mesa receipt](bcm2712-v3d-gl.md#physical-mesa-acceptance-2026-10-11).
