# Native hostname

The ordinary tools graph builds `nbhostname` from the unchanged
[bin/hostname](../../bin/hostname/Makefile) source. The host entry uses the
normal generated `nbtool_config.h` and libnbcompat interfaces. This provides
`setprogname` and `getprogname` on hosts whose libc lacks those BSD APIs.
The selected native compiler builds and links the program; target headers
and target libraries do not supply native interfaces.

`TOOL_HOSTNAME` selects this build output for kernel identity generation.
`newvers.mk` passes it explicitly to `newvers.sh`, like `TOOL_DATE` and
`TOOL_AWK`. The script invokes it without operands. Direct script use keeps
the existing `hostname` default. No hostname setter is used by the build.

The older [bootstrap.json](bootstrap.json) describes a bounded direct-libc
source bootstrap. It remains separate from the ordinary tools graph and is
not an admission requirement for an installed Clang or GCC prefix. Its native
libc requirements must hold before that historical route can execute.

## Read-only checks

Build and install the normal `tools/hostname` target. Then run with SDK Python:

```sh
"$PYTHON" tools/hostname/test-native.py \
  --sdk-source "$SDK_SOURCE" --host-prefix "$HOST_PREFIX" \
  --hostname /absolute/tooldir/bin/nbhostname --make "$NETBSD_MAKE" \
  --work "$NEW_OUTPUT"
```

The check invokes only empty argv, `-s` and invalid option `-z`. It compares
the read results with the host's read-only `gethostname` value. It never
passes a hostname operand. It also runs the actual kernel identity script
under the routed tools and checks that the selected hostname and build
identity appear in the generated source. No target code executes.
