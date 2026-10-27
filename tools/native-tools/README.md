# Native tool routing

`build.sh` can use a native toolset selected by an external SDK producer.
Set `EMBER_NATIVE_TOOLS` to an absolute routing directory.
Invoke `build.sh` with the selected absolute shell path.
Do not use its `env sh` shebang for this mode.

The interface version is 1.
The static `contract` file belongs to this OS source tree.
It is data, not a shell program or a toolset qualification descriptor.
The first line is `EmberBSD-native-tools 1`.
Each later line has three fields separated by one space:

```text
command native-role binding
```

`command` is the fixed executable name in the routing directory.
`native-role` names the selected SDK tool role.
`binding` names a `HOST_*` variable, `MAKE`, `SED`, or `-`.
`MAKE` selects the NetBSD make implementation.
`SED` selects the quoting utility used by `build.sh`.
`HOST_YACC` selects Byacc before the compatibility library is built.
That library generates the complete OS date parser before `tools/yacc` exists.
`HOST_MV_CP` and `HOST_MV_RM` bind the child tools compiled into host `mv`.
The generated make wrapper preserves both selected paths.
`printf` is required when the selected shell has no built-in `printf`.
`echo` is required by make's direct execution of simple recipes, including
the initial build-variable query. A shell builtin does not supply that path.
`-` makes the command available through the selected `PATH` only.
The contract file contains the complete initial list of required roles.
It includes utilities needed before the OS versions are built: `grep` for
Clang detection, and `basename`, `diff`, `egrep`, `expr`, `ls`, `mv`, and
`rmdir` for the initial configure scripts. `touch`, `find`, and `xargs`
support the native configure and GNU host-tool make rules. Install these
from the common native Ports recipes before publishing the routing directory.
`cut` supplies Flex version fields. `uniq` preserves the definitions emitted by
Autoconf when generating configuration headers. `nm` keeps native symbol probes
on the selected LLVM or GNU implementation instead of an absolute system fallback.

The SDK must publish a routing directory with these entries:

- `contract`: A regular file with the exact bytes of this source contract.
- Each `command`: A symbolic link to the absolute selected native-tool spelling.

Do not resolve a selected in-bundle symbolic link before creating the mapping.
A compiler can locate its support files from its invocation path.
Keep the routing directory and all selected tool inputs immutable.
Hold their SDK read leases until all child processes have drained.
Publish the directory atomically through the SDK publication interface.
Do not append ambient directories to its `PATH`.

The OS checks protocol compatibility and routing completeness.
The SDK checks the selected host prefix, executable architecture, and tool routes.
It supplies native ABI flags. No qualification receipt or tool hash is required.
This interface does not copy the SDK native-toolset schema.
The OS accepts absolute paths with characters from
`A-Z a-z 0-9 _ . / + @ % = -` only.
It refuses other characters before it runs selected tools.
This bound protects the existing shell and make wrapper representation.

`build.sh` checks each required symbolic link before it runs external commands.
It then sets `PATH` to the routing directory only.
The selected `cmp` checks the contract copy.
The selected `readlink` gets the exact absolute executable paths.
The script binds the host compiler variables and shell variables to these paths.
It refuses command-line changes to the routing variables.
Host ABI flag variables remain available to the SDK caller.

The script uses the selected NetBSD make directly.
It ignores a stale make in `TOOLDIR` and does not copy the selected make.
The generated wrapper retains the selected make path and tool environment.
It refuses `-r` and `USETOOLS=no` in this mode.
Refresh make through the native-tool producer.
The old make bootstrap uses configure scripts with absolute host-tool probes.
It cannot meet this interface through a `PATH` change alone.

Every real `build.sh` operation requires this selected native-tool routing
before sanity checks or make bootstrap. Help, parse-time queries and `-n`
remain available without it. Use the SDK's
[`scripts/run-native-os.py`](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/native-os.md)
for OS operations; it selects the native prefix and supplies the routing
directory. This also applies to metadata operations such as `show-params`.

`share/mk/sys.mk` selects the recipe shell with `.SHELL` before shell queries.
`HOST_SH`, `BSHELL`, `CONFIG_SHELL`, and `SHELL` use the selected shell.
The make engine does not select its recipe shell from those variables alone.

## Compiler families

`EMBER_COMPILER_FAMILY=clang|gcc` selects target tools; the default is Clang.
`build.sh -c clang|gcc` sets this value and preserves it in its make wrapper.
`EMBER_HOST_COMPILER_FAMILY=clang|gcc` selects native compiler options
independently. The SDK passes this value with the matching host prefix.
Selecting GCC is explicit and never a fallback for an unsupported Clang target.

The routing protocol accepts executable paths, not command strings. For LLVM,
its `as` role must select the common Ports `clang-as` frontend, which adds
integrated-assembler arguments with the selected native shell. A direct `as`
symlink to Clang would lose those arguments. Target commands use the separate
`EXTERNAL_TOOLCHAIN` interface, with explicit target and linker selection.

Run the real make selection regression with an explicit host NetBSD make:

```sh
"$PYTHON" tools/native-tools/test-compiler-family.py --make "$NETBSD_MAKE" -v
```

It checks LLVM command arguments, explicit GCC routing, independent host and
target DTrace flags, reproducible path maps, and invalid input rejection.
These are make metadata checks, not compiler, runtime, kernel or device tests.

## Current validation boundary

This first pilot covers `build.sh`, make selection, wrapper generation, and
dispatch to the `tools` make target.
It does not qualify a native toolset or all nested configure routes.
Generated `TOOLDIR` tools remain build outputs for the selected host.
Some tools makefiles add `TOOLDIR/bin` to `PATH`.
Third-party configure scripts can call absolute host paths.
Use the selected prefix for those commands and fix any external tool selection.
A known missing tool or incorrect route must report its concrete error.
Operations that build target code also require explicit `EXTERNAL_TOOLCHAIN`
from the selected SDK compiler prefix. `tools` may omit it only when the
resolved make configuration has `TOOLCHAIN_MISSING=yes`, `MKLLVM=no`, and
`MKLLVMRT=no`; the in-tree tools graph otherwise includes compiler or runtime
targets. Artifact-only operations such as sets, source sets, and image packing
do not need a target compiler prefix. These checks select the producer
boundary; they do not validate the SDK compiler prefix's origin or contents.

Run the control-flow checks with one explicit test shell and Python:

```sh
"$PYTHON" tools/native-tools/test.py --shell "$TEST_SHELL" -v
```

The tests execute the actual `build.sh` with synthetic tools and make.
Run `test-make.py --make /selected/bmake --shell /selected/sh --echo /selected/echo`
with native executables to check the initial variable-query recipe through
the contract's routed command names.
They replay its generated wrapper and check refusal cases.
They do not run configure, a compiler, or a native-tool qualification probe.
They do not prove that a real NetBSD make uses the selected recipe shell.
Run that check with make from the selected host prefix.
Use `--build-sh /path/to/older/build.sh` with
`RoutingTests.test_tools_dispatch_and_wrapper_replay` as a causal negative control.

Run `test-configure.py --source "$OS_SOURCE" --sdk-source "$SDK_SOURCE"`
`--host-prefix "$HOST_PREFIX" --work "$NEW_OUTPUT"` with SDK Python to exercise
the real Texinfo configure script under the same restricted command mapping.
It requires the resulting standard-header definitions. The old mapping can
exit zero while silently losing them; `--expect-broken-header` reproduces that
failure without treating an unrelated configure failure as the expected result.

Check the early configure paths and real `tools/compat` and `tools/gmake`
Clang-detection queries with explicit test utilities:

```sh
"$PYTHON" tools/native-tools/test-bootstrap-routing.py \
  --make "$NETBSD_MAKE" --shell "$TEST_SHELL" \
  --utility-path "$TEST_UTILITY_PATH" -v
```

Only commands named by the OS contract enter the test `PATH`. The compiler
fixture reports a Clang version and rejects every compilation. These checks
cover configure argument parsing and arrival at that expected compiler error;
they do not establish a successful configure or build. `--contract` accepts
an older contract for a causal negative control. To check only the missing
`grep` failure, select `BootstrapRoutingTests.test_real_makefiles_detect_clang`.

The native build route passes `MACHINE` and `MACHINE_ARCH` as make command-line
inputs. This also overrides a Ports make's compiled-in host architecture.
The generated cross wrapper retains those inputs for recursive make calls.
Run `test-target-identity.py --sdk-source "$SDK_SOURCE" --host-prefix "$HOST_PREFIX"`
`--work "$NEW_OUTPUT"` with SDK Python. It creates and queries a real AArch64
wrapper without compiling tools or target code.

## Common host BFD

With an external toolchain, set `EMBER_HOST_BFD_CONFIG` to the absolute
`host-bfd.mk` emitted by the common Ports Binutils producer. The kernel wrapper
requires this input and passes it to `build.sh`. The tools graph then uses the common
BFD headers and static link dependencies for `dbsym` and `mdsetimage`, and
omits its old local Binutils provider. With Clang, `tools/binutils` only
installs the common producer's `prefix/bin/TRIPLET-elfedit` into the existing
OS tool path used by `tests/compat/linux`. The generated BFD `BUILD` directory
and installed `prefix` retain their common work root. Keep the complete common producer
result while these consumers use it. No application sysroot is needed.

An explicitly selected missing input, target mismatch or missing dependency
fails instead of selecting the old provider. An unset variable retains the
existing in-tree route for generic make or `build.sh` invocations. An explicit
`EMBER_NATIVE_TOOLS` mapping with an external compiler requires the common
input, as do the Ember kernel wrapper and SDK producers. This does not
change target compiler selection or native userland Binutils builds.

```sh
"$PYTHON" tools/native-tools/test-host-bfd.py --make "$NETBSD_MAKE" \
  --install "$NATIVE_PREFIX/bin/bsdinstall" -v
```

The regression queries the real make graph, include flags and dependencies.
It checks both compiler families, native helpers and invalid inputs. With
`--install`, it copies an elfedit fixture through the real install target.
Its synthetic headers and archives are not compiled. Build and execute the helpers with real common
BFD outputs before claiming native helper acceptance.

For actual helper execution, run:

```sh
"$PYTHON" tools/native-tools/test-host-bfd-runtime.py \
  --binutils-prefix /absolute/common-binutils/prefix \
  --dbsym /absolute/native-dbsym --mdsetimage /absolute/native-mdsetimage \
  --work /absolute/new-helper-check
```

The test assembles an AArch64 ELF fixture by default; `--target amd64` selects
the AMD64 producer instead. Add `--elfedit /absolute/installed-elfedit` to
check the OS tool's Linux OSABI edit without changing other ELF bytes.
It checks symbol-table embedding,
RAM-disk insertion/extraction and oversized-image refusal without target
execution. On Linux, inspect `ldd` separately to confirm that compression
libraries resolve inside the selected native prefix.

On Linux, native `ls` requests XSI `wcwidth` declarations while preserving the
normal glibc namespace. Run `tools/ls/test-native.py --make "$NETBSD_MAKE"`
with SDK Python to syntax-check the actual source and make-selected flags.
Add `--ls /absolute/native-ls` to check UTF-8 and control-character filenames
under the installed `C.UTF-8` locale.

External LLVM dependency commands pass `--target` separately from caller-owned
`MKDEPFLAGS`. Native program and library dependency rules omit target flags.
`mkdep` executes the `CC` pathname and does not forward fixed arguments from
that environment value. Normal compiler commands keep their explicit target.
Run `test-dependency-target.py --make "$NETBSD_MAKE" --work "$NEW_OUTPUT"`
with SDK Python to exercise real C, C++, Objective-C and assembly dependency
commands against an AArch64-only header. No target code executes.

The command roster supplies `mktemp` for the actual `genassym` script and
`head` for nested configure checks. The generator's `-d` mode invokes the
supplied dependency command without adding assembly-output flags. Kernel
`assym.d` uses this mode and the selected dependency target flags.

Run the real generator and dependency regression with SDK Python:

```sh
"$PYTHON" tools/native-tools/test-genassym.py \
  --sdk-source "$SDK_SOURCE" --host-prefix "$HOST_PREFIX" \
  --mkdep /absolute/native-nbmkdep --work "$NEW_OUTPUT"
```

It checks AArch64 constants, a real dependency header, conflicting mode
refusal and the routed `head` command. It does not execute target code.
