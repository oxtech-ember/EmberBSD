# FDT boot memory discovery

`fdt_memory_get` discovers physical memory ranges from the device tree.
It first searches for `device_type = "memory"`. If no such node exists,
it keeps the name-based `/memory` fallback from the Device Tree specification.
The function ignores empty ranges and returns the minimum start and maximum
end, including holes. Register decoding and range storage keep their own
existing contracts.

Only `-FDT_ERR_NOTFOUND` is a successful end of node iteration.
Other libfdt errors cause a panic with the parser error name. Do not pass a
negative parser result back as a node offset. A negative start offset makes
libfdt start again at the root. Repeating a structural error can stop boot in
an endless loop. A parser error after a valid range is also fatal. A partial
range list must not be accepted as complete physical memory.

This behavior does not identify the source of a damaged tree. The loader,
copy operation and platform fixups require separate evidence.

## Checks

Run `python3 ember/tools/fdt-memory-contract.py` with host `CC` set to GCC16.
The test compiles the actual `fdt_memory_get`, phandle conversion functions
and owning libfdt sources. It models register decoding and range publication.
It covers valid single and multiple ranges, the name fallback, missing and
empty memory, malformed first and trailing nodes, and an invalid header.
The trailing-node case first publishes one range, then requires a parser
panic. A host watchdog detects repeated calls to the actual libfdt iterator.

Use `--case first` or `--case tail` for the two structural error cases.
Use `FDT_MEMORY_CFLAGS` for analyzer or sanitizer flags. Use `--emit DIRECTORY`
to retain the generated fixture. The shared `fdt_contract.py` runner also
serves the bootstrap trace check. Native register decoding, memory allocation
and physical boot remain separate checks.
