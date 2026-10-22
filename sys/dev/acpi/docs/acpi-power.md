# ACPI PowerResource ownership

`acpi_power_res(resource, owner, on)` uses a process-context sleep mutex.
Its signature is unchanged. The resource registry lives until shutdown.
Registry lookup and creation are serialized. No resource lock is held while
acquiring the registry lock. Deregistration snapshots the boot-long entries
before releasing ownership through the same checked control path.

The first owner invokes `_ON`. A confirmed resource accepts duplicate and
additional owners without repeating `_ON`. Releasing a shared owner does not
invoke `_OFF`. The last owner invokes `_OFF` before removing its reference.

An AML failure can follow a partial hardware change. The driver keeps the
owner and records an uncertain state. That owner can retry `_ON` or request
`_OFF` for rollback. Another owner receives `AE_ABORT_METHOD` until the state
is confirmed. A failed `_OFF` also retains its owner. Only successful `_OFF`
removes the final owner. Unknown releases are rejected without AML.
The initial unowned state does not establish the firmware's physical state.

`acpi_power_switch` stops at a resource-control error and returns it.
Previously acquired references remain available for explicit cleanup.
The void deregistration interface cannot report a failed final release;
its failed owner remains recorded. This is not an atomic D-state engine.
The `_PR0`/`_PR3` overlap behavior, ignored `_PSx` result and resource-order
policy in `acpi_power_set` remain outside this change.

## Source contract

Use GCC16 on a supported host:

```sh
CC=gcc CFLAGS=-fanalyzer sh ember/tools/acpi-power-contract.sh
CC=gcc CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/acpi-power-contract.sh
```

The contract selects complete production functions and the fork's queue
header. It models ACPICA-shaped values, namespace, allocation, mutex and AML
services. A target compilation separately checks the native header contract.
It checks first/duplicate/shared/last ownership, uncertain-state retries,
rollback, capacity, deregistration, error propagation and two controlled
thread interleavings. A timer bounds the host process. The registry test
allows a serialized implementation to finish without blocking its fixture.
`ACPI_POWER_SOURCE` can select a preserved source file for a causal baseline.
The pre-fix source fails the same executable assertions.

`ACPI_POWER_TEST_OUTPUT` retains the executable at the selected path.
`COMPILE_ONLY=1` requires that path and skips execution. `TEST_RUNNER` selects
one executable path. Use a wrapper script for Valgrind options or other commands.
`ACPI_POWER_ANALYZE=1` also runs Cppcheck and host Clang Static Analyzer.
It stores compiler predefines and analyzer logs beside the executable.
Without a retained output path, those temporary files are removed on exit.
Inherited source deadstores remain visible. An analysis with diagnostics exits
unsuccessfully; that is distinct from a passing execution contract.

The checks do not run a native kernel or real AML. They do not establish
board power, clock, reset or VPU readiness. A caller must still qualify the
actual methods, firmware resources and recovery behavior on its target.
