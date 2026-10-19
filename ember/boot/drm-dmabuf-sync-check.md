# Native DMA-BUF sync-file smoke test

`ember/tools/drm-dmabuf-sync-check.c` tests the native ioctl path on a PowerVR
render node. It needs the native PRIME and DMA-BUF sync-file implementation.
It creates private 4 KiB buffers. It does not submit a GPU job or use a display.

Build from a clean EmberBSD source commit with the shared GCC16 compiler and
a matching EmberBSD userland sysroot. No library other than libc is required:

```sh
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror \
    -I"$SOURCE/sys/external/bsd/drm2/dist/include/uapi" \
    -I"$SOURCE/sys/external/bsd/drm2/powervr" \
    "$SOURCE/ember/tools/drm-dmabuf-sync-check.c" -o "$OUTPUT"
```

The probe includes the native `sys/sys/dmabuf.h` from the same source tree.
Run the binary with one render-node argument. Select the node for the intended
PowerVR device. The process has a 120-second alarm and one-second poll bounds.
Any error returns a nonzero exit status. Preserve stdout, stderr and the status.

Each of 128 cycles checks both read/write and read-only PRIME descriptors:

- Close the GEM handle before export from its PRIME descriptor.
- Export an empty reservation and require a readable, close-on-exec sync file.
- Create a `DRM_SYNCOBJ_CREATE_SIGNALED` object. Export its actual sync file,
  destroy its handle, then import READ, WRITE and RW dependencies where allowed.
- Re-export each admitted dependency and require exactly POLLIN.
- Reject zero or unknown flags, invalid descriptors, and a PRIME or DRM
  descriptor used as a sync file. Failed export must preserve the output fd.
- Reject WRITE and RW on a read-only descriptor with EACCES. Permit READ.
- Close every owned descriptor and handle. Compare the native F_MAXFD/F_GETFD
  descriptor count after every complete cycle.

A PASS covers completed-fence ioctl dispatch, basic descriptor ownership,
rights and descriptor-count stability. It does not prove pending-fence waits,
error propagation, GPU completion, implicit BO usage, CPU cache maintenance,
DMA_BUF_IOCTL_SYNC, scanout or a desktop session. Those need separate evidence.
The probe does not export an unsignaled binary syncobj with no fence and does
not fabricate a pending or error fence.

Run `python3 ember/tools/drm-dmabuf-sync-check-contract.py` for the host control.
It executes the actual probe with bounded syscall models and actual ABI headers.
It checks missing ioctl dispatch, absent CLOEXEC, poll timeout/error, accepted
invalid flags or wrong identities, lost rights, descriptor leakage, fcntl
failure, allocation failure and cleanup. Use DRM_CFLAGS for extra host flags.
These causal controls are not a kernel or hardware acceptance test.
