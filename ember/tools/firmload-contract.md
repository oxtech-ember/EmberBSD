# Firmware read contract check

`firmload-contract.py` compiles the current source of `firmware_read`,
`firmware_read_alloc`, and both Wi-Fi consumers. It replaces the vnode reader,
allocator, and filename services with deterministic models. It keeps each
source license in the generated translation unit.

Select GCC 16 and an existing build directory on the host:

```sh
CC=gcc-16 python3 ember/tools/firmload-contract.py --work /path/to/build/obj
```

Set `FIRMLOAD_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for a sanitizer run. Set `FIRMLOAD_CFLAGS=-fanalyzer` for GCC path analysis.
Use `--keep` to retain the generated source and executable for another analyzer.
Use `TEST_RUNNER='valgrind --leak-check=full --error-exitcode=1'` on a supported host.
The tool removes its temporary directory after the run unless `--keep` is set.

The cases check invalid sizes, caller limits, the `vn_rdwr` integer limit,
allocation failure, read failure, buffer ownership, and handle ownership.
They call the real bwfm and aicwf functions. They check model-file selection,
default fallback, missing files, and failure after a successful open.
File selection, image formats, and chip upload protocols remain in the drivers.
The public allocation contract is in `firmload(9)`.

To reproduce the empty-file failure before this change, use a parent revision:

```sh
CC=gcc-16 python3 ember/tools/firmload-contract.py \
    --work /path/to/build/obj --revision PARENT --consumers-only
```

The old aicwf consumer must fail the allocator's positive-size check.
The same command must pass against the changed revision.
An invalid compiler command or a missing revision is not regression evidence.

These are host contract checks. They do not test vnode I/O, kernel allocation,
device firmware, SDIO transfers, or hardware attachment. Run the kernel build
and device integration checks separately. A host with 64-bit `size_t` checks
the explicit limit values; it does not prove a 32-bit target build.

Origin: EmberBSD; AI-assisted implementation and tests. The interface extends
NetBSD `firmload(9)`. OpenBSD `loadfirmware(9)` provides prior art for caller-owned
whole-file buffers. No OpenBSD code is copied.
