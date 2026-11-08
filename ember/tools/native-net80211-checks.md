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

The RXBA native fixture exercises the real legacy PCI soft interrupt and
detach paths with `sc_executor == NULL`; entering its async-only linkage seams
is a test failure. Async RX admission and key-epoch delivery, executor drain,
and asynchronous detach use their separate signal-queue, key-epoch and
executor contracts. These deterministic host models do not prove hardware or
concurrent scheduler behavior.

GCC analyzer checks are separate diagnostics. Select an installed native GCC
prefix explicitly with `--compiler-family gcc`; the `*-analysis.sh` entry
points retain their `-fanalyzer` checks. For a diagnostic selected by an
environment variable, use the selected `env` role to pass it to the script,
for example `--tool env -- RXBA_ANALYZE=1 sh
ember/tools/net80211-rxba-contract.sh`. Do not silently substitute GCC when a
Clang run fails. For checks that invoke Clang Static Analyzer and Cppcheck,
add `--required-role clang --required-role cppcheck`; SDK supplies the `CSA`
adapter and selected `cppcheck`. A direct caller may set `CSA`; otherwise the
scripts run `clang --analyze` from `PATH` and fail if it is absent. The analyzer
is never skipped. `net80211-csa-contract.py` checks that each workflow running
these scripts installs clang or sets `CSA`, and that the selected analyzer rejects
a causal NULL dereference. The GCC analyzer remains selected through `CC` and
its `-fanalyzer` flags.

The station SAE fixtures run together through `ember/tests/net80211-sae.sh`.
The script checks the native kernel owner, all BSD supplicant variants and
the actual RTL8852BE link-loss/state-transition paths,
using `SAE_TEST_CFLAGS` and the selected `CC`/`PYTHON`. Set `SAE_ANALYZE=1`
to run Clang Static Analyzer and Cppcheck on their generated compilation
databases, retaining each variant's real definitions and include paths.
Cppcheck receives source preprocessed by the recorded compiler, so its view
uses that compiler's actual builtins and native system headers. The mandatory
SAE categories match the existing CCMP policy: warnings, performance and
portability, without an additional library model. A separate POSIX-model run
on the Darwin guard-page fixture reports a model limitation: it rejects the
valid anonymous `mmap` descriptor `-1` and treats failure as `NULL` instead
of `MAP_FAILED`. Do not change the valid guard-page source to fit that model.
The analyzer helper's `--style` option adds an advisory style
run and still returns nonzero for findings; upstream const suggestions and
callback signatures must be reviewed without changing imported APIs merely
to satisfy style. Existing analyzer policies are unchanged.
In the explicit GCC diagnostic profile, add `-fanalyzer` to
`SAE_TEST_CFLAGS`; Clang's separate pass uses its own analyzer.

The common `ember/tests/net80211-bip.sh` and AIC compatibility
`ember/tests/aicwf-bip.sh` contracts additionally require `pkg-config` and
OpenSSL 3 or later for independent host cryptography. Their flag variables
are `NET80211_BIP_CFLAGS` and `AICWF_BIP_CFLAGS`; their respective
`NET80211_BIP_ANALYZE=1` and `AICWF_BIP_ANALYZE=1` switches enable both source
analyzers. All three runners honor `TEST_RUNNER` for Memcheck execution.
The main net80211 workflow runs these fixtures in its existing optimized
Memcheck, static-analysis, and fatal ASan/UBSan phases. Its manual A-MSDU
diagnostic-capture workflow remains separate from acceptance checks.
