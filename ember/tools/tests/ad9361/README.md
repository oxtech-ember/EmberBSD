# Native AD9361 host regression

Origin: EmberBSD tests for the ADI no-OS adapter at
`ce7107f957d19858f2dffc85bf3d7f2c70367000`.

Run from the EmberBSD source root:

```sh
AD9361_SANITIZE=1 sh ember/tools/tests/ad9361/native-test.sh "$PWD"
AD9361_VALGRIND=1 sh ember/tools/tests/ad9361/native-test.sh "$PWD"
```

Use separate runs for AddressSanitizer/UndefinedBehaviorSanitizer and Valgrind.
Set `AD9361_LOG` to a file path to retain the complete output. The script removes
its temporary source copies and executable when the run ends.

The harness compiles the actual native adapter and imported no-OS source. It
replaces only the kernel includes with small host shims. The allocation shim
models the documented non-failing `KM_SLEEP` contract and aborts on host OOM.

The synthetic SPI backend checks the RX-only write policy and acknowledges
calibration and PLL states. The TX cases require a separate bus initializer
and an explicit FDT opt-in. They cover fixed-delay bootstrap, fresh register
status, attenuation, mute, retune, rejected quadrature non-convergence,
transport failures, sticky errors, reset and cleanup. Each bootstrap SPI
transaction is independently failed. The test verifies that no SPI transaction
follows the first transport failure.

The fastlock contract caches at most 128 TX profiles in native memory. Prepare
requires maximum attenuation and zero DAC samples. It calibrates quadrature at
index zero and accepts other frequencies only within the no-OS calibration
threshold from that frequency. It finishes with index zero selected. Stage
loads and reads back an inactive hardware bank while the active bank is left
unchanged. Recall accepts only the staged index and requires zero DAC samples.
Its lock polling delays total at most 100 microseconds; SPI latency is additional.
It performs no ordinary retune or calibration. Ordinary retune discards the
cache and explicitly leaves fastlock mode, including a previously cached rate.

The synthetic fastlock RAM rejects every write to an active bank. Tests cover
128-entry bounds, profile readback, exact staged-index matching, selected-bank
drift, lock timeout, teardown, and every prepare/stage/recall SPI failure. These
checks do not establish a physical hop-time bound or RF spectral performance.

Synthetic acknowledgements do not validate RF output, AD9361 digital eye
margins, external clocks, DMA, FPGA timing or a physical receiver. The native
TX status keeps `digital_tuning_valid` false when it uses fixed FDT delay taps.

## Exclusive TX provider

```sh
AD9361_WRAPPER_SANITIZE=1 sh ember/tools/tests/ad9361/wrapper-test.sh "$PWD"
AD9361_WRAPPER_VALGRIND=1 sh ember/tools/tests/ad9361/wrapper-test.sh "$PWD"
AD9361_WRAPPER_ANALYZE=1 sh ember/tools/tests/ad9361/wrapper-test.sh "$PWD"
```

This harness extracts the actual SPI attachment and TX lease functions.
Attachment and RX keep a rate of no more than 1 MHz. TX preparation restores
that conservative rate for each lease. After RESETB release and the ADI reset
settling interval, it reads PRODUCT_ID. It then applies the validated FDT rate
and checks that the complete ID byte is unchanged. The second transfer also
checks controllers that defer configuration until their first transaction.

The tests inject configuration failures, transport failures at both rates,
and an ID mismatch that preserves the product-family mask. Each failure
keeps the first transport error, asserts reset and prevents reuse of that
lease. A new lease repeats the conservative probe. Rates below 1 MHz remain
unchanged. These host checks do not qualify a physical board's SPI timing.
