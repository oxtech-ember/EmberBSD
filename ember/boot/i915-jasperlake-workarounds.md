# Jasper Lake workaround adaptation

This extends the [guarded source foundation](i915-jasperlake.md).
Native attachment remains disabled.
The changes are personal source adaptations, not hardware acceptance.
All imported driver files retain Intel's MIT notices and authorship.
The adaptation and host regression are AI-assisted with OpenAI Codex.

## Display power and FBC

FBC requires a four-line-aligned sum of plane height and Y offset on Gen11+.
The existing Y-offset alignment check still applies.
JSL selects the existing EHL combo-PHY AUX power-well table.
It must not use ICL Type-C AUX operations for combo ports C and D.

The late suspend handler sets `SBCLK_RUN_REFCLK_DIS` after DC9 entry.
The early resume handler clears it after DC-state sanitization and DC9 exit.
This adaptation limits the new sequence to JSL with real JSP or MCC PCHs.
Other platforms retain their existing suspend/resume paths.
An earlier upstream toggle in IRQ reset was insufficient for Gen11 low-power entry.
This port uses the corrected lifecycle and leaves IRQ reset unchanged.

| Upstream commit | Author and scope |
| --- | --- |
| [`a21906ead6a59419613f803460ca900f2560b1e1`](https://github.com/torvalds/linux/commit/a21906ead6a59419613f803460ca900f2560b1e1) | José Roberto de Souza: FBC WA22010751166 |
| [`e8ab8d669d046a8e9b07707d2f00b9ba3e25d0ae`](https://github.com/torvalds/linux/commit/e8ab8d669d046a8e9b07707d2f00b9ba3e25d0ae) | Matt Roper: EHL/JSL combo-PHY power wells |
| [`b896898c73696a7bbe6af36e8d5e8f394a5013f2`](https://github.com/torvalds/linux/commit/b896898c73696a7bbe6af36e8d5e8f394a5013f2) | Bob Paauwe and Anshuman Gupta: corrected Gen11 WA14010685332 sequence |
| [`8b46cc6577f4bbef7e5909bb926da31d705f350f`](https://github.com/torvalds/linux/commit/8b46cc6577f4bbef7e5909bb926da31d705f350f) | Anshuman Gupta: remove the obsolete IRQ toggle |

| Upstream file and revision | SHA256 |
| --- | --- |
| `display/intel_fbc.c` at `a21906ead6a5` | `fced8b64b5dbfd3efa90e10793c42c7a6a00b5a4dd5c89887c020d1c0ec1c024` |
| `display/intel_display_power.c` at `e8ab8d669d04` | `dfb2f2799efe1b32156ae5c9f6082feb97174a5c91af239ed595edcc2f2fb4a3` |
| `display/intel_display_power.c` at `b896898c7369` | `9a67df3f98a1c6b94c28d9d2d3ef94beb36d192cdba9517e70d9a89e9349101b` |
| `i915_reg.h` at `b896898c7369` | `014618b89496ce2c93221bc90a567c803175dbb9b93dc4c8842748d997c1b242` |
| `display/intel_display_power.c` at `8b46cc6577f4` | `4abe3c63c75d29a7adb64491c6833c5e55bf92dd6de1a592be00845b54f19d1d` |
| `i915_irq.c` at `8b46cc6577f4` | `36bbdf8b843de0773450703a56efabf64300a5e8bc796d7da8ee17b9e9110f46` |

## Regression boundary

```sh
ruby ember/tests/i915-jsl-power.rb
```

The regression executes the real FBC predicate, power-well selector and suspend/resume functions.
Host hooks supply device state, record ordered calls and preserve unrelated register bits.
It covers height/offset residues, JSP/MCC and absent/wrong PCHs, EHL/ICL and an older power path.
The baseline fails on the FBC rule; semantic mutations fail executable assertions.
The default native run uses ASan/UBSan; native GCC16 also runs `-fanalyzer`.
Valgrind is unavailable on this macOS host; the sanitizer run is a separate check.
Target GCC16 uses the real GENERIC flags and headers for isolated translation units.
These checks do not establish working MMIO, suspend residency, a full kernel or physical graphics.

The five-TU Cppcheck baseline and candidate both report 312 findings.
There are no new normalized source findings.
The inherited reports include 42 signed register-mask shifts and one GNU-expression AST error.
The AST error limits coverage; this is not a clean Cppcheck result.
Other inherited findings remain outside this adaptation with the OS i915 owner.
Full-TU GCC16 path analysis reports no diagnostics in the five-TU baseline or changed power/FBC candidates.

## GT workarounds and reset

WA1607087056 applies to all ICL revisions, but only revision-zero A0 on JSL/EHL.
The adaptation uses the existing `IS_REVID` predicate without a new public stepping interface.
WA22010271021 disables RCS fixed-function DOP clock gating on JSL/EHL.
This scoped addition follows the EHL fix and JSL split; existing ICL behavior stays unchanged.

JSL resets each requested domain twice, stopping immediately if either acknowledgement fails.
Each JSL acknowledgement has a 2000-microsecond timeout.
A 50-microsecond settling delay follows success or failure.
Other platforms keep one reset, their 500-microsecond timeout and no settling delay.
The upstream reset change applies more broadly; this personal adaptation is explicitly JSL-only.

| Upstream commit | Author and scope |
| --- | --- |
| [`61b088c5374a9f886efa1edbb49ce552bd1f9cba`](https://github.com/torvalds/linux/commit/61b088c5374a9f886efa1edbb49ce552bd1f9cba) | Swathi Dhanavanthri: A0-only WA1607087056 on EHL/JSL |
| [`537af0b0caf4b18bf0414ff5ffb4d3d86633403c`](https://github.com/torvalds/linux/commit/537af0b0caf4b18bf0414ff5ffb4d3d86633403c) | Matt Atwood: RCS WA22010271021 |
| [`24ea098b7c0d80b56d62a200608e0b029056baf6`](https://github.com/torvalds/linux/commit/24ea098b7c0d80b56d62a200608e0b029056baf6) | Tejas Upadhyay: common JSL/EHL GT predicates |
| [`3db9d590557da3aa2c952f2fecd3e9b703dad790`](https://github.com/torvalds/linux/commit/3db9d590557da3aa2c952f2fecd3e9b703dad790) | Chris Wilson and Andi Shyti: repeat reset and let engine state settle |

| Upstream file and revision | SHA256 |
| --- | --- |
| `gt/intel_workarounds.c` at `61b088c5374a` | `d8224f00615beaac3da8959eda3ecdbe33b858a3b2952da6b782d2f7af117714` |
| `gt/intel_workarounds.c` at `537af0b0caf4` | `553cbf4fb1070c9b5fb66274dd0139f5bdf7fa659ebedf64f9323ba311b06e2c` |
| `gt/intel_workarounds.c` at `24ea098b7c0d` | `33e9681842fbee398c2f17752d399ffab0b5f624394baf0ffef71f7e84ad5c0a` |
| `gt/intel_reset.c` at `3db9d590557d` | `265cbe3f7c0a2000885ddda360d5530d85b10fae0ce15f87601385eafcb48298` |

```sh
ruby ember/tests/i915-jsl-gt.rb
```

The test executes real workaround builders and the hardware-domain reset function.
It imports the register definitions; hooks record WA masks, reset writes, waits and delays.
Independent expectations check A0/B0+, ICL and older platform controls.
Reset checks include domain-mask preservation, first/second acknowledgement failure and delay ordering.
The host test uses the target's `-fwrapv` semantics for inherited signed register-mask constants.
The baseline fails an RCS assertion; eight semantic mutations fail executable assertions.
ASan/UBSan, native GCC16 analysis, real target compilation and full-TU path analysis pass.
The five-TU Cppcheck comparison still retains only its baseline findings and coverage limits.
No hook establishes real MMIO completion or engine recovery on hardware.
