# Native fixed-mode BO snapshot

`bcmv3d-kms-snapshot` copies one diagnostic framebuffer BO into a PPM P6 file.
It does not prove that these bytes reach the physical PV0/HVS output. It adds
no kernel API and performs no private register access, modeset or master ioctl.
Use it only within the isolated graphics test whose contents you may capture.

## Admitted input

Select a plane ID from the existing DRM resource inventory. The test controller
checks the expected live KWin PID and owner, pinned binaries and launch
configuration, the active DRM backend/output and the unchanged isolated
session/transport.

Native DRM uses cloned DTYPE_MISC descriptors. Use unfiltered `fstat -p PID`
for descriptor context. Its generic misc rows identify neither the primary
versus render node nor master ownership. A pathname filter such as
`fstat -p PID /dev/dri/card0` suppresses these descriptors. Header-only output
from that filter does not show that the primary card is closed.

These observations do not provide an atomic query for another process's exact
primary fd or master ownership. Continued primary-card and master ownership
by that KWin are explicit assumptions. Run the diagnostic as root with a
second fd on the configured card. Legacy GETFB otherwise returns handle zero
to a nonmaster. The program does not issue SET_MASTER or DROP_MASTER. Opening
an unowned primary can acquire master implicitly. Do not use SET_MASTER as an
ownership probe. Keep the compositor active and exclude planned logout or
restart during the copy. A concurrent ownership change remains a diagnostic
limit.
Do not run it after logout: logical BLANK no longer exposes the previous active
framebuffer. The program does not retry frame acquisition.

The recorded native [KMS whitelist](bcmv3d-kms.md) admits one full-screen
1920x1080 XRGB8888 plane, LINEAR, pitch 7680 and offset zero. The probe checks
driver name `v3d`, one advertised XR24 format, active nonzero FB/CRTC IDs and
GETFB width/height/pitch/depth24/bpp32. Legacy GETFB cannot report modifiers or
plane offsets. LINEAR/offset0 are inferred from the matched native whitelist,
not independently queried. Other kernels/providers are outside this contract.
GETFB2 is not implemented by the retained DRM core.

The standard GETPLANE, GETFB, PRIME_HANDLE_TO_FD, MAP_DUMB, CRTC_GET_SEQUENCE
and GEM_CLOSE ioctls supply metadata, a retained handle and the native UVM
mapping. The existing
public V3D_WAIT_BO ioctl waits at most one second on that same handle. This
wait observes native GPU busy/error state; it is not an implicit-writer fence
snapshot or a CPU reader lease. The external test controller must also bound
all execution, including potentially blocking ioctls and filesystem operations.

With the selected plane ID and a new path in a private directory, the CLI is:

```sh
./bcmv3d-kms-snapshot /dev/dri/card0 31 /absolute/new/image.ppm
```

The plane number is an example. Use the actual inventory for the matched kit.

## Capture and lifetime

The program exports the retained GEM handle with PRIME_HANDLE_TO_FD and
DRM_CLOEXEC. It queries the actual BO extent with `lseek(fd, 0, SEEK_END)`.
The native dma-buf seek implementation supplies this size; its fstat operation
does not. A failed export or size query rejects the capture. The extent must
cover the 8,294,400 frame bytes, be aligned to the admitted 4 KiB or 64 KiB CPU
page size, and not exceed the native 256 MiB BO limit. The program rejects
unaligned or signed-overflowing mmap offsets.

The program maps that entire BO read-only with MAP_SHARED. It copies only the
8,294,400 frame bytes into 6,220,800 RGB bytes. It does not copy BO padding.
The output records both the queried BO extent and the mapping length. Native
XR24 bytes are B,G,R,X; X is ignored. PPM carries opaque RGB and has no alpha
dependency.

Mapping only the frame can be shorter than its BO. A native mapper with an
exact-size lookup and a NULL-object success fallback can return anonymous
zero pages for that request. An older frame-sized snapshot can therefore be
false black. Querying and mapping the full extent is an independent consumer
check. It does not replace repair of that mapper contract or identify a
physical black-screen or GPU-cache cause.

GETPLANE is checked before and after the copy. A changed FB/CRTC, blank plane,
wrong shape, ioctl error or backwards CRTC sequence/time rejects the capture.
The CRTC sequence may advance during a copy without a BO change. The output
records the sequence/time interval; it does not claim a single PV epoch.

A GEM handle and mapping retain storage, not immutable pixels. GETPLANE reads
software atomic state, which can become visible before native publication and
latch. Equal IDs do not exclude reuse or an A-to-B-to-A transition. Standard
queries do not join FB identity with the actual PV active-list address. A
compositor-coordinated completion/quiescence point is needed for a stronger
association. A black BO snapshot alone does not identify a black-screen cause.

The native pager passes BUS_DMA_PREFETCHABLE to bus_dmamem_mmap. The AArch64
path maps it as write-combining Normal Non-cacheable memory. Successful native
GPU completion performs its cache/DMA POST work before clearing busy. This
supports a cold CPU read after that observed completion, but does not exclude
a later writer or CPU producer. mmap itself performs no fence wait or cache
synchronization. DMA_BUF_IOCTL_SYNC and PRIME begin/end_cpu_access hooks are
absent in this provider. No new coherency guarantee follows from this probe.

Device mapping, PRIME fd, GEM handle and card fd cleanup finish before the
output is created. The PRIME fd is closed on every path after a successful
export, including size-query and mapping failures.
The program opens a new file with O_EXCL and mode 0600. It never overwrites an
existing path. Short writes are completed and zero-length writes fail. Write,
fsync or close failure unlinks only the file created by this invocation. An
unlink failure is reported; any remaining partial file is not an accepted
snapshot. Use an existing mode-0700 output directory controlled only by the test
controller. The program does not validate the parent directory or protect
against another actor that can rename entries there.

## Build and source checks

Build from a clean pinned checkout with the common GCC16 and recorded EmberBSD
sysroot. The static consumer needs no libdrm or PNG library:

```sh
CC=/absolute/gcc16/bin/aarch64--netbsd-gcc SYSROOT=/absolute/emberbsd-sysroot \
    sh ember/tools/bcmv3d-kms-snapshot-build.sh /absolute/source /absolute/new-output
ruby ember/tools/bcmv3d-kms-snapshot-contract.rb /absolute/source
```

The host contract compiles the actual diagnostic with controlled DRM and OS
services. It verifies all RGB bytes, X-byte handling, queried BO extents, padded BOs,
PRIME fd cleanup, 4/64 KiB mapping bounds,
frame changes, short writes, errors and cleanup. Named compiled controls must
reach their exact failed assertions. Signals, watchdog expiry and sanitizer diagnostics fail the run, even with
an expected exit or assertion. Nine host process controls check this gate.
Set SNAPSHOT_EVIDENCE to a new directory to retain source pins and command logs.
CC and CFLAGS select the host compiler/instrumentation. Host checks do not
execute native DMA, the target pager, hardware scanout or the compositor.

After a separately authorized target run, retain the kernel/profile identity,
plane and FB metadata, sequence brackets, exit status and PPM hash. This is a
BO diagnostic result. Physical display acceptance still needs its own evidence.
