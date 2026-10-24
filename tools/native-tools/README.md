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
`-` makes the command available through the selected `PATH` only.
The contract file contains the complete initial list of required roles.

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
The SDK owns tool qualification, identities, hashes, ABI flags, and lifetime.
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

`share/mk/sys.mk` selects the recipe shell with `.SHELL` before shell queries.
`HOST_SH`, `BSHELL`, `CONFIG_SHELL`, and `SHELL` use the selected shell.
The make engine does not select its recipe shell from those variables alone.

## Current validation boundary

This first pilot covers `build.sh`, make selection, wrapper generation, and
dispatch to the `tools` make target.
It does not qualify a native toolset or all nested configure routes.
Generated `TOOLDIR` tools need separate provenance and closure checks.
Some tools makefiles add `TOOLDIR/bin` to `PATH`.
Third-party configure scripts can call absolute host paths.
These routes need an accepted producer contract before a production build.
The SDK sysroot and release execution gates must stay closed until that proof.
This OS interface is not permission to bypass them.

Run the control-flow checks with one explicit test shell and Python:

```sh
"$PYTHON" tools/native-tools/test.py --shell "$TEST_SHELL" -v
```

The tests execute the actual `build.sh` with synthetic tools and make.
They replay its generated wrapper and check refusal cases.
They do not run configure, a compiler, or a native-tool qualification probe.
They do not prove that a real NetBSD make uses the selected recipe shell.
That check must use the qualified make after native-toolset admission.
Use `--build-sh /path/to/older/build.sh` with
`RoutingTests.test_tools_dispatch_and_wrapper_replay` as a causal negative control.
