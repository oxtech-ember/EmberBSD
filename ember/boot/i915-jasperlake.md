# Jasper Lake i915 source foundation

This is a source foundation for a local experimental port.
It registers only Intel PCI device `8086:4e55` as `INTEL_JASPERLAKE`.
Default kernels and standalone modules reject preliminary JSL attachment.
The separate `GENERIC_JSL_TRIAL` configuration permits only the exact identity described below.
This source contract does not establish working graphics.

## Scope and provenance

The platform split adapts Linux commit
[`24ea098b7c0d80b56d62a200608e0b029056baf6`](https://github.com/torvalds/linux/commit/24ea098b7c0d80b56d62a200608e0b029056baf6),
by Tejas Upadhyay, reviewed by Matt Roper.
The imported files retain Intel's MIT notices.
The native stolen-memory table retains its BSD notice and attribution.
The adaptation and regression are AI-assisted with OpenAI Codex.

The upstream split contains more PCI IDs and newer driver interfaces.
This adaptation adds only `4e55` and preserves the existing EHL ID table.
It uses this tree's `engine_mask` field.
This tree keeps Gen11 SSEU initialization in `intel_device_info.c`,
rather than upstream's `gt/intel_sseu.c`.

| Input at the upstream commit | SHA256 |
| --- | --- |
| `include/drm/i915_pciids.h` | `b5856f41ab4b2e9e79c8ef08b910ac5d2f2e4d418bfefe9eef387561331af60a` |
| `drivers/gpu/drm/i915/i915_pci.c` | `d861cffe1a14e788a4a6fd452f306f1493fde3e7f276d19eec1edf259c52e425` |
| `drivers/gpu/drm/i915/i915_drv.h` | `22de572c1e0e1ebd24f9f7254bff5369bb565321451fbcc1a80f97a500fc678e` |
| `drivers/gpu/drm/i915/intel_device_info.c` | `c5d8c12c4f35a293bb2bb9df0427392dfb7490f1b7b3c8b1b3335ae946e63ec3` |
| `drivers/gpu/drm/i915/intel_device_info.h` | `60a9e4f1b594b5c8df051adddd4c59613737d146d2a1f24d6f13eb4c08712e37` |
| `drivers/gpu/drm/i915/intel_pch.c` | `3e607e60691c0559cd3e0377da6e8af34972bac7aecb668791a0bae021c98a74` |
| `drivers/gpu/drm/i915/gt/intel_sseu.c` | `93fac1e3a700b603a9469553f05cbb7ba5ac7e2b366f0d7ae5c4bd9b0f925760` |

The dedicated JSL device info keeps `require_force_probe = 1`.
The native PCI lookup rejects preliminary hardware unless the exact personal-trial exception applies.
The source registers Gen11 stolen-memory operations for the new ID.
It shares SSEU geometry and PCH checks through `IS_JSL_EHL`.
It preserves distinct `IS_ELKHARTLAKE` and `IS_JASPERLAKE` predicates.

## Display source adaptation

The display foundation shares JSL/EHL predicates in PHY, output, PLL and CDCLK selection.
It adds JSL low-swing eDP tables for HBR and HBR2.
eDP and low-voltage DP stop at HBR2; higher-voltage DP can use HBR3.
VBT link-rate limits still apply.
JSL/EHL PHYs support eDP 1.3, so the configuration rejects PSR2.

These MIT-licensed changes adapt the following upstream commits:

| Commit | Author and scope |
| --- | --- |
| [`24ea098b7c0d80b56d62a200608e0b029056baf6`](https://github.com/torvalds/linux/commit/24ea098b7c0d80b56d62a200608e0b029056baf6) | Tejas Upadhyay: common JSL/EHL display predicates |
| [`1ba1014db2586bdfe0a9680a00613e2089586c46`](https://github.com/torvalds/linux/commit/1ba1014db2586bdfe0a9680a00613e2089586c46) | Tejas Upadhyay: JSL eDP swing tables, BSpec 21257 |
| [`8ee8167771da66558cd71c8b59afd2a47160333f`](https://github.com/torvalds/linux/commit/8ee8167771da66558cd71c8b59afd2a47160333f) | Animesh Manna: EHL/JSL source rates, BSpec 32247 and 20598 |
| [`c5c874a8352918555c79b8af5a25f7e543c84844`](https://github.com/torvalds/linux/commit/c5c874a8352918555c79b8af5a25f7e543c84844) | Edmund Dea: reject PSR2 on JSL/EHL |

This tree uses the older `dev_priv`, output-type and rate interfaces.
The existing `icl_get_combo_buf_trans` dispatcher selects the JSL tables.
Both voltage-limit calculation and register programming use that dispatcher.
The adaptation preserves the native `I915_READ` access convention.
It does not import Linux encoder-state or MMIO interfaces.

| Upstream display file and revision | SHA256 |
| --- | --- |
| `intel_ddi.c` at `24ea098b7c0d` | `bec1eae33fea43a039da5b3d25f72e9b70eed724550348ecddd88baf798cb1a0` |
| `intel_dp.c` at `24ea098b7c0d` | `9c4cf4d69303239ddb7d48c331338353b8dfb90160457285d7a3db55e30b0228` |
| `intel_psr.c` at `24ea098b7c0d` | `ef342365667a7a3489cb77b333f3e0999ff019c6a5e14020fc001be23d21f753` |
| `intel_combo_phy.c` at `24ea098b7c0d` | `10039b3f5c9f1759c98e288e415f5c9d311a03948036d57d11588288233cfa0a` |
| `intel_display.c` at `24ea098b7c0d` | `39509dd2cf52c73a356a0eb98ecd554675ef989be75bcf3281f0b8a3e7295c60` |
| `intel_dpll_mgr.c` at `24ea098b7c0d` | `ad80656c3e89ae21bad3f4427129929ea83a3fd29295da67b025f2c9d9574880` |
| `intel_cdclk.c` at `24ea098b7c0d` | `1a0a62daa7f601aaf72c8ebf18497c05012e5369f21400618ef5cd0a33ab4178` |
| `intel_ddi.c` at `1ba1014db258` | `01e0f61e9287ea9d811f68e05a4c69c20591418746e5d2ca55325b00fd7bced7` |
| `intel_dp.c` at `8ee8167771da` | `c0304241449be6cd40b6c1b73991bee0f1ff7d9a596fd7dd0a00d7199e948edf` |
| `intel_psr.c` at `c5c874a83529` | `67128e3095bba801dd7a796f1732d00ecd7f024347ff380e3127b31cd6daad81` |

## Regression and evidence limits

Run from the source root with a native C compiler:

```sh
ruby ember/tests/i915-jsl-platform.rb
ruby ember/tests/i915-jsl-display.rb
sh ember/tools/drm-native-identity-contract.sh
```

The Ruby test compiles real PCI and stolen-memory tables.
It extracts the production platform-mask, platform-name, SSEU, PCH and native lookup functions.
It supplies host models for MMIO, inherited Gen11 feature fields and SSEU storage.
It checks JSL identity, consumers, existing EHL/ICL behavior and the closed attach guard.
Its default host run uses AddressSanitizer and UndefinedBehaviorSanitizer.
`HOST_CC` and `HOST_CFLAGS` select another native compiler or analysis mode.
Full target TU compilation checks the inherited fields and real kernel headers.

The display regression runs real table selectors and source-rate calculations.
It also executes PHY mapping, PLL-manager selection, CDCLK limits and PSR2 validation.
Expected JSL swing values are independent literals from the cited upstream table.
Models supply the device structures, fuse-register reads and PLL descriptors.
The test covers HBR/HBR2 boundaries, per-PHY voltage, VBT caps and EHL/ICL controls.
It does not execute actual MMIO writes or validate an electrical display signal.

The missing-ID baseline fails the executable identity test.
Target compiler and static-analysis checks concern isolated translation units.
They do not establish a full kernel build, boot, modesetting, rendering or desktop operation.
GCC16 analysis retains four baseline shift-count warnings in `sseu_set_eus`.
The real setter computes `eu_stride` from EUs per subslice, not subslice count.
JSL passes eight EUs, producing stride one and only a zero-bit shift.
The extracted setter/init/compute/store chain passes all 8192 JSL fuse combinations under ASan/UBSan.
The analyzer loses that invariant; an explicit-bound diagnostic control passes.
Production SSEU code and warning settings remain unchanged.
Separate baseline analysis retains 15 NULL findings in DDI and one in display.
Their normalized source locations and diagnostic counts do not change.
Those findings remain with the OS i915 owner outside this bounded adaptation.
Cppcheck retains three baseline AST errors and 48 signed-shift reports in register masks.
The AST errors limit its coverage of GNU expressions in DDI, DP and display.
The report has no new findings; it is not a clean or complete analysis result.

## Personal trial configuration

`sys/arch/amd64/conf/GENERIC_JSL_TRIAL` includes GENERIC and sets `EMBER_I915_JSL_4E55_TRIAL`.
The option is off in GENERIC and INSTALL.
The native source includes its generated option header only under `_KERNEL_OPT`.
Manually loading the default standalone module keeps the rejection; use the separate trial kernel.

The exception checks the existing function-zero, Intel-vendor and display-class gates first.
It requires product `4e55`, an Intel table entry, platform `INTEL_JASPERLAKE`, generation 11,
a 36-bit PPGTT size and the existing RCS0/BCS0/VCS0/VECS0 engine mask.
The platform field is the pre-attach equivalent of the later runtime `IS_JASPERLAKE` predicate.
Non-preliminary attachment keeps its existing behavior.

Run the real guard and generated-header checks with a native NetBSD config tool:

```sh
NETBSD_CONFIG=/absolute/nbconfig ruby ember/tests/i915-jsl-trial.rb
```

Without `NETBSD_CONFIG`, the test models the option headers and states that generation was not checked.
The test executes 2304 identity/require-flag combinations for each of GENERIC, INSTALL, trial and module states.
The real config tool confirms default headers omit the option and only the trial header defines it.
The baseline rejects the trial identity; eight semantic mutations fail executable assertions.
ASan/UBSan, native GCC16 analysis and both real target TU option states pass.
Target GCC16 `-fanalyzer` reports no diagnostics in the baseline or either candidate state.
Cppcheck retains 180 baseline findings with the default option off.
Trial mode adds one style report from the inherited `BIT` macro at the engine-mask check.
That macro bounds the shift using an unsigned type width; its constant width cannot be negative.
The four engine indices are 0, 1, 2 and 6, and their expected mask is `0x47`.
No diagnostic suppression or register-mask change is added.
The inherited Cppcheck reports remain with the OS i915 owner.

This stage was prepared from merge `1745f04ba4063486b4eef176dbe8b7bcd853052a` with upstream `ad698a6e40ea89d72249f76d06913be29a5ab90c`.
A planned image must offer the stock GENERIC kernel as a distinct recovery choice.
Keep each kernel and its modules from the same pinned build.
Boot-menu integration, the complete trial build and physical acceptance are separate stages.

## Execlist submission queue

Gen11 engines take context descriptors through the ELSQ registers.
Each port holds one 64-bit descriptor in two dwords.
Port `n` has its lower dword at `RING_EXECLIST_SQ_CONTENTS + n * 8` and its upper dword at `+ n * 8 + 4`.
The native `write_desc()` uses a byte offset for `submit_reg`, not a Linux `u32 __iomem` pointer.
The previous code added `port * 2` and `port * 2 + 1` bytes.
Because of this, most writes went to wrong, unaligned offsets, and the engines did not start.
On a Jasper Lake laptop, `intel_gt_wait_for_idle()` then timed out in `__engines_record_defaults()`.
The fix multiplies the dword index by `sizeof(u32)`.
Pre-Gen11 ELSP keeps its upper-then-lower writes to one register.

```sh
sh ember/tools/i915-elsq-desc-contract.sh
```

The contract extracts the real `write_desc()` and logs each `bus_space_write_4()` call.
It checks ELSQ offsets and values for ports 1 and 0, 4-byte alignment and the ELSP order.
The previous code fails the first ELSQ offset check.
With the fix, a JSL trial kernel completes GT initialization without the idle timeout.
Rendering acceptance is a separate stage.

## GT lock lifetime after a failed initialization

`intel_gt_init_early()` creates the GT wakeref and the RPS locks.
The `intel_gt_init()` error path calls `intel_gt_pm_fini()`.
`intel_gt_driver_release()` calls it again during detach.
When `intel_gt_init()` returns `-EIO`, `i915_gem_init()` marks the GT wedged and keeps KMS.
Because of this, the display stays attached after the failure.
`intel_prepare_plane_fb()` then calls `intel_rps_mark_interactive()`, which takes `rps->lock`.
Previously, `intel_gt_pm_fini()` destroyed these locks.
Because of this, the first framebuffer task used a destroyed mutex and the kernel panicked before the console started.
Now `intel_gt_driver_late_release()` destroys the wakeref and RPS locks once.

```sh
sh ember/tools/i915-rps-lifetime-contract.sh
```

The contract extracts `intel_rps_init_early()` and the real fini and release functions.
It models `intel_gt_pm_init_early()`, the error sequence and each mutex state.
It covers a failed GT initialization with display use, a normal detach and an early probe failure.
It requires one destroy for each lock and no use after destroy.
The previous code fails the first use after `intel_gt_pm_fini()`.
A JSL kernel with the RPS part of the fix reaches the console after the GT failure.
The wakeref part has no hardware test.

## Remaining port work

The [workaround adaptation](i915-jasperlake-workarounds.md) adds FBC, power, GT and firmware selection.
GuC remains disabled by default.
The separate trial configuration admits only `8086:4e55` with the expected JSL device information.
Other preliminary devices keep the existing rejection.
The image must retain a separate stock recovery kernel before a physical trial.

The first physical scenario requires the actual PCI revision, subsystem IDs,
firmware revision and internal panel interface.
It targets confirmed eDP or HDMI, native KMS and an Iris-rendered Wayland session.
Native MIPI-DSI is outside this scenario.
No MOCS extension, GuC activation or UAPI change is part of this foundation.
Firmware inclusion, full kernel/module builds and physical acceptance remain pending.
