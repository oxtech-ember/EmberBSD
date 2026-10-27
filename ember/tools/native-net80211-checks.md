# Native net80211 and CCMP checks

The ordinary host contracts use the selected native Ports compiler. Run them
through the SDK `run-native-check.py` entry point, which selects Clang by default,
binds the compiler and native ABI arguments, and restricts the tool search path.
The owner scripts require `CC` explicitly; a direct caller is responsible for
selecting its tools and does not gain SDK selection checks merely by setting it.

For example, with a caller-owned existing work directory and new log and bindings
paths:

```sh
/absolute/host-pkg-llvm/bin/python3.15 -B \
  /absolute/EmberBSD-SDK/scripts/run-native-check.py \
  --host-prefix /absolute/host-pkg-llvm --tool sh \
  --cwd /absolute/EmberBSD --log /absolute/work/wme.log \
  --bindings /absolute/work/wme-bindings \
  -- ember/tools/net80211-wme-contract.sh
```

Supply native ABI flags explicitly using the SDK's `--cflag`, `--cppflag`,
`--cxxflag`, and `--ldflag` options when needed by the host. The same entry
point runs the other shell contracts. Pass `--required-role awk` for the PHY IE
and VHT contracts, which invoke awk. Use `--tool python` for Python contracts,
passing their documented arguments after `--`. Bindings and log paths must be
new for each run. Tests execute host fixtures; they do not establish target or
hardware behavior.

GCC analyzer checks are separate diagnostics. Select an installed native GCC
prefix explicitly with `--compiler-family gcc`; the `*-analysis.sh` entry
points retain their `-fanalyzer` checks. For a diagnostic selected by an
environment variable, use the selected `env` role to pass it to the script,
for example `--tool env -- RXBA_ANALYZE=1 sh
ember/tools/net80211-rxba-contract.sh`. Do not silently substitute GCC when a
Clang run fails. For checks that invoke Clang Static Analyzer and Cppcheck,
add `--required-role clang --required-role cppcheck`; SDK supplies the `CSA`
adapter and selected `cppcheck`. The GCC analyzer remains selected through
`CC` and its `-fanalyzer` flags.
