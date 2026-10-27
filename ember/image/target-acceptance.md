# Disposable AArch64 target root

The SDK target acceptance runner assembles a disposable root from the selected
OS build's `base.tar.xz`. `target-acceptance-root.sh` requires the same build's
`DESTDIR/etc/MAKEDEV`, `TOOLDIR/bin/nbmtree` and `TOOLDIR/bin/nbmakefs`. It
adds the standard console/null/tty devices and the explicitly selected `ld`
device emitted by MAKEDEV, then creates a new FFS image. It does not create
host device nodes or modify an existing image.

The base set omits `/etc/rc` and the ordinary rc configuration in the separate
etc set. The SDK places `target-acceptance-rc` directly at `/etc/rc` in this
disposable diagnostic root; it avoids the full multiuser startup path and its
root remount logic. Init opens `/dev/console` before invoking the runcom, so the
root includes MAKEDEV's standard devices. The diagnostic rc validates its nonce
and mounted root device, requires read-only FFS, uses tmpfs for test scratch,
runs the shared Ports `run-target.sh` bundle, and powers off. The root unit and
partition are explicit SDK inputs; this source does not encode a presumed QEMU
device number.

Run the fixture without creating an FFS image:

```sh
HOST_SH=/absolute/native-ports-prefix/bin/mksh
HOST_AWK=/absolute/native-ports-prefix/bin/nawk
export HOST_SH HOST_AWK
"$HOST_SH" ember/image/test-target-acceptance-root.sh
```

Run it in the SDK's `native_checks.check_environment()` environment so utility
aliases such as `awk` resolve to the selected native tool and `PATH` contains
only the native prefix and its generated bindings. The fixture uses `HOST_AWK`
only when it generates a MAKEDEV script from source.

After an OS build has produced its tools, the same test script can also
exercise the real `nbmtree` and `nbmakefs` in a temporary directory. If
`EMBER_OS_DESTDIR` is supplied, its `etc/MAKEDEV` is used; otherwise the fixture
generates `MAKEDEV` from the selected source tree. This creates only a small
disposable FFS image and does not assert guest boot acceptance:

```sh
HOST_SH=/absolute/native-ports-prefix/bin/mksh
HOST_AWK=/absolute/native-ports-prefix/bin/nawk
EMBER_OS_TOOLDIR=/absolute/os-build
# Optional when using the source-generated fixture MAKEDEV:
# export EMBER_OS_DESTDIR=/absolute/os-build/dest
EMBER_OS_SOURCE=/absolute/EmberBSD
EMBER_ROOT_DEVICE=ld0a
export HOST_SH HOST_AWK EMBER_OS_TOOLDIR EMBER_OS_SOURCE EMBER_ROOT_DEVICE
"$HOST_SH" ember/image/test-target-acceptance-root.sh
```
