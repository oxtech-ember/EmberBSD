# Native rm bootstrap

Origin: EmberBSD (AI-assisted). The canonical C source retains its BSD license.

The ordinary [host recipe](Makefile) uses [bin/rm/rm.c](../../bin/rm/rm.c)
with normal host compatibility. It does not disable libnbcompat.
The finite [contract](bootstrap.json) uses native GCC16 and the selected host ABI.
It needs the shared [annotation header](../compat/bootstrap_cdefs.h).

The Darwin path preserves O_SYNC and O_NOFOLLOW. It requests checked F_NOCACHE
after descriptor identity checks when Darwin has no O_RSYNC. Other hosts keep O_RSYNC.
An unsupported cache-control request fails before any overwrite or unlink.
F_NOCACHE can still use cached paths for small or misaligned operations.
It is not a replacement for O_RSYNC or a physical-erasure guarantee.
The four write passes, zero-pattern readback, explicit flushes and errors remain unchanged.
See [Apple fcntl](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html).

The 25 finite cases use isolated typed fixtures and exact filesystem assertions.
Ordinary hardlinks retain deterministic peer bytes and link identity.
The -P cases use unique files of 0, 1, 8192 and 8193 bytes.
Their removal result does not prove every overwrite pass or physical erasure.
Random final hardlink-peer bytes have no exact-byte oracle in this contract.
Whiteout error cases retain the feature; successful whiteout mutation needs filesystem support.

Run `python3 -B tools/rm/test-source.py` from an exact source snapshot.
This source-only check executes no compiler or native rm.
Native API behavior, separate pass/failure diagnostics, C analysis, memory and
ordinary host/target qualification remain pending. No diagnostic hooks enter the finite producer.
