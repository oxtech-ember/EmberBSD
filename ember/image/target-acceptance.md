# Disposable AArch64 target root

The SDK target acceptance runner assembles a disposable root from the selected
OS build's `base.tar.xz`. `target-acceptance-root.sh` requires the same build's
`DESTDIR/etc/MAKEDEV`, `TOOLDIR/bin/nbmtree` and `TOOLDIR/bin/nbmakefs`. It
adds only the explicitly selected `ld` device metadata emitted by MAKEDEV and
creates a new FFS image; it does not create host device nodes or modify an
existing image.

The guest starts through `/etc/rc.local`, validates its nonce and the mounted
root device, requires the root FFS to be read-only, then uses tmpfs for test
scratch space. `target-acceptance-rc` runs the shared Ports `run-target.sh`
bundle and powers off after completion. The root unit and partition are
explicit SDK inputs; this source does not encode a presumed QEMU device
number.

Run the fixture without creating an FFS image:

```sh
sh ember/image/test-target-acceptance-root.sh
```

After an OS build has produced its tools, the same test script can also
exercise the real `nbmtree` and `nbmakefs` in a temporary directory. If
`EMBER_OS_DESTDIR` is supplied, its `etc/MAKEDEV` is used; otherwise the fixture
generates `MAKEDEV` from the selected source tree. This creates only a small
disposable FFS image and does not assert guest boot acceptance:

```sh
EMBER_OS_TOOLDIR=/absolute/os-build/tools \
# Optional when using the source-generated fixture MAKEDEV:
# EMBER_OS_DESTDIR=/absolute/os-build/dest \
EMBER_OS_SOURCE=/absolute/EmberBSD \
EMBER_ROOT_DEVICE=ld0a \
sh ember/image/test-target-acceptance-root.sh
```
