# External GCC16 runtime for OS cross builds

The optional `EMBER_EXTERNAL_GCC_RUNTIME` make variable selects an audited
GCC 16.2.0 target runtime outside `DESTDIR`. This adapter supports the
`x86_64--netbsd` and `aarch64--netbsd` targets without multilib. It does not
install a compiler, runtime package, C++ headers or GCC startup objects.

Use the application cross compiler from EmberBSD-Ports. Configure it against
a private sysroot with the matching EmberBSD seed and staged GCC runtime.
The kernel-only compiler is not an application compiler. It disables threads.
Keep the seed revision, runtime bootstrap receipt, archive hashes and compiler
configuration together. This make adapter checks the required paths, not
their provenance or ELF architecture. Audit those inputs before selecting them.

The selected runtime directory must contain:

- `include/c++`, its target configuration directory and `backward` headers.
- `lib/gcc/TRIPLET/16.2.0/crtbegin.o`, `crtbeginS.o`, `crtend.o` and `crtendS.o`.
- `lib/gcc/TRIPLET/16.2.0/libgcc.a` and `libgcc_eh.a`.
- `lib/libgcc_s.so.1`, `libstdc++.so.7`, its `libstdc++.so` linker alias,
  `libstdc++.a` and `libsupc++.a`.

Keep this directory in the build tree. Use a new OS `DESTDIR`. Do not copy
these development files into the release destination to satisfy a link.

## Selection

Add these options to the normal `build.sh` distribution command:

```sh
-V EXTERNAL_TOOLCHAIN=/absolute/application-toolchain \
-V TOOLCHAIN_MISSING=yes -V MKGCCCMDS=no -V MKCOMPAT=no \
-V EMBER_EXTERNAL_GCC_RUNTIME=/absolute/bootstrap/stage/usr/pkg/gcc16
```

The external compiler must be GCC. Keep `MKCXX=yes`. Do not set
`HAVE_GCC=16`; that variable selects the imported in-tree compiler sources.
The external-toolchain rules disable those sources and their support libraries.
Host compilers and host flags remain separate from this target selection.
The adapter also excludes rules with preexisting `-nostdlib` in `LDFLAGS`,
or `_STANDALONE` or `_KERNEL` in `CPPFLAGS`. Set these flags before including
the build rules. Raw-linker firmware and freestanding programs must not acquire
application-runtime headers, startup objects, library paths, or RPATH.

The adapter adds target header paths and isolates the C++ standard headers
with `-nostdinc++`. It adds GCC CRT and library search paths to program and
library links. A libc-style `-nodefaultlibs -lgcc` link selects the staged
`libgcc.a`. Explicit `LIBSTDC++` and `LIBSUPC++` archive dependencies also use
the stage. The OS still builds and selects its own `crt0.o`, `crti.o` and
`crtn.o`. The only added runtime RPATH is `/usr/pkg/gcc16/lib`.
The staged runtime directory also supplies a build-only `-rpath-link` search.
This resolves indirect C++ dependencies, such as a C program that links libproc.
It does not add a build path to the target RPATH. A libc-style `-nodefaultlibs`
link remains eligible for the adapter and its staged static libgcc.

The final system receives target DSOs through the separate `gcc16-runtime`
Ports package and normal target pkgtools. Select one GCC runtime provider.
Build-tree headers, CRT and static archives are not an installable compiler
package. Image composition and runtime execution checks remain separate.

## Regression checks

Run the actual BSD make metadata checks with the configured wrapper:

```sh
sh ember/tests/external-gcc-runtime.sh /absolute/tools/bin/nbmake-amd64 \
  /absolute/os-source
```

This checks both target mappings, program/library flags, static dependencies,
OS startup-object ownership, unchanged defaults, host isolation and rejected
mixed, missing, relative, foreign-compiler and multilib inputs.
It also rejects runtime flags in freestanding, kernel, and `-nostdlib` contexts.

After the audited runtime and application compiler exist, run the bounded
cross-link checks. They do not execute the target programs:

```sh
sh ember/tests/external-gcc-runtime.sh /absolute/tools/bin/nbmake-amd64 \
  /absolute/os-source --links /absolute/application-toolchain \
  /absolute/bootstrap/stage/usr/pkg/gcc16 /absolute/ember-sysroot \
  /absolute/new-link-results
```

The second command links C, C++ thread/exception and shared-library fixtures.
It generates real C++ dependency files with the selected target headers.
An indirect C-to-C++ DSO link requires the staged build-only search path.
The C executable must not gain a direct libstdc++ dependency.
It also checks a `-nodefaultlibs -lgcc` library. It saves build logs, ELF
metadata and linker maps. The maps must select staged GCC CRT and libgcc
with OS `crti/crtn`. ELF checks reject build paths in RPATH and require the
selected C++ SONAME. Run the prebuilt programs on EmberBSD separately.
Metadata checks alone do not establish successful target links or execution.

Isolated replays of the amd64 world link commands pass for libproc's two
test programs and dtrace. Both EFI replays remove the compiler-only flags
from raw `ld` and produce ELF files without runtime dependencies or RPATH.
They still report `PHDR segment not covered by LOAD segment` and an RWX
segment warning. The existing `--noinhibit-exec` option permits output and
exit status zero despite the PHDR error. These are not clean EFI links.
The final PE files do not use ELF program headers. UEFI boot must establish
whether the converted images work; neither output creation nor exit status
zero establishes that result.
The original commands expose the missing indirect runtime and the
compiler-only flags passed to raw `ld`.
These replays reuse existing target objects without changing the common build.
The adapter does not change the bootxx size limit. A control on source
`70d994c4798f` kept its old 8200-byte payload identical after flag removal.
A bootxx size repair, toolchain linker-script relocation, and full-world
validation are separate changes.
