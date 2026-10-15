# Jasper Lake i915 identity foundation

This is a source foundation for a local experimental port.
It registers only Intel PCI device `8086:4e55` as `INTEL_JASPERLAKE`.
It does not enable native attachment or establish working graphics.

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
The unchanged native PCI lookup still rejects preliminary hardware.
The source registers Gen11 stolen-memory operations for the new ID.
It shares SSEU geometry and PCH checks through `IS_JSL_EHL`.
It preserves distinct `IS_ELKHARTLAKE` and `IS_JASPERLAKE` predicates.

## Regression and evidence limits

Run from the source root with a native C compiler:

```sh
ruby ember/tests/i915-jsl-platform.rb
sh ember/tools/drm-native-identity-contract.sh
```

The Ruby test compiles real PCI and stolen-memory tables.
It extracts the production platform-mask, platform-name, SSEU, PCH and native lookup functions.
It supplies host models for MMIO, inherited Gen11 feature fields and SSEU storage.
It checks JSL identity, consumers, existing EHL/ICL behavior and the closed attach guard.
Its default host run uses AddressSanitizer and UndefinedBehaviorSanitizer.
`HOST_CC` and `HOST_CFLAGS` select another native compiler or analysis mode.
Full target TU compilation checks the inherited fields and real kernel headers.

The missing-ID baseline fails the executable identity test.
Target compiler and static-analysis checks concern isolated translation units.
They do not establish a full kernel build, boot, modesetting, rendering or desktop operation.
GCC16 analysis retains four baseline shift-count warnings in `sseu_set_eus`.
The identity-only adaptation does not change that function.
Those findings remain with the OS i915 owner for a separate assessment.

## Remaining port work

Native attachment stays closed until the display predicates, JSL PHY tables,
link-rate limits and required display/GT workarounds have separate review.
The next steps must preserve a stock recovery kernel.
Only the final opt-in trial may permit attachment for `8086:4e55`.
Other preliminary devices must keep the existing rejection.

The first physical scenario requires the actual PCI revision, subsystem IDs,
firmware revision and internal panel interface.
It targets confirmed eDP or HDMI, native KMS and an Iris-rendered Wayland session.
Native MIPI-DSI is outside this scenario.
No MOCS extension, GuC activation or UAPI change is part of this foundation.
Firmware inclusion, full kernel/module builds and physical acceptance remain pending.
