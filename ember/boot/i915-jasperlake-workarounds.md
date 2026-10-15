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
