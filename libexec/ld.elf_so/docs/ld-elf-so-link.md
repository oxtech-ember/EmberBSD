# Relative loader links

`libexec/ld.elf_so` installs the loader in `SHLINKINSTALLDIR`, normally `/libexec`.
When that directory is not `/usr/libexec`, it also installs links:

| Link | Target |
| --- | --- |
| `/usr/libexec/${PROG}` | `../..${SHLINKINSTALLDIR}/${PROG}` |
| `${DEBUGDIR}/usr${BINDIR}/${PROG}.debug`, with `MKDEBUG` | `../..${BINDIR}/${PROG}.debug` |

`PROG` is `ld.elf_so`, or `ld.elf_so-${MLIBDIR}` for a compat loader.
`BINDIR` is `SHLINKINSTALLDIR`.
The relative targets resolve to the loader on an installed system,
in a `DESTDIR` and in a cross sysroot.
The previous absolute targets pointed out of a sysroot to the build host.
The EmberBSD-SDK sysroot builder rejects such links.
If `/usr` is itself a symbolic link, `..` follows its target directory.
A system with that layout needs a separate check.

```sh
sh ember/tools/ld-elf-so-link-contract.sh
```

The check extracts the actual `SYMLINKS` entries of the conditional block.
It resolves each target from its link directory for the native and a compat loader.
It rejects an absolute target. The previous Makefile fails it.
An amd64 EmberBSD-SDK sysroot build on macOS completes with the change.
That sysroot and an installed system have no target execution check yet.
