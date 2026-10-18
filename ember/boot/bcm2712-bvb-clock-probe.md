# Finite BCM2712 BVB0 clock probe

The existing [opt-in module](bcm2712-display-hsm.md#finite-opt-in-module) accepts
`clock-mode=bvb0`. An absent property keeps the existing HSM-only mode.
Empty, unknown, non-string or conflicting clock/DVP/HPD/DDC modes fail before any effect.
The selected kernel must contain the [native HSM/BVB owner](bcm2712-native-display-clocks.md).
Audit the module against that kernel's exact source, headers, options and strong symbols.
Buildinfo alone does not prove ABI. The optional ABI sample includes the BVB status layout.

Use the existing cold-headless, framebuffer, render and selected-build guards.
Shut down, remove HDMI and perform a controlled cold boot before this probe.
Removing the cable from a running firmware console does not satisfy that prerequisite.
Set `kernel_commit` from the verified selected-kernel receipt before this command:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    -s clock-mode=bvb0 \
    /absolute/path/bcm2712_hsm_probe.kmod
```

The mode requests an HSM port0 minimum of 149985000 Hz for the 148.5 MHz 1080p clock contract.
It verifies the acquired HSM state, lease, minimum and cached rate before the first BVB call.
The HSM lease remains held throughout BVB creation, status, acquisition, release and destruction.
Other modes retain their existing 120 MHz HSM minimum.

The BVB sequence is finite:

1. Create the native BVB owner. This step sends only GET requests.
2. Record initial cached state, rate, firmware floor, leases, fault and mutation.
3. Acquire port0 with a 75000000 Hz minimum. Record and validate cached status.
4. Release port0 to the firmware-derived unleased floor. Record status before destroy.
5. Destroy BVB, then release and destroy HSM. Do not read a freed owner.

The native core validates packet fields, current maximum and rounded rate readbacks.
The requested minimum is not an exact measured clock frequency.
The BVB unleased floor is the positive firmware MIN cached at creation, not a guessed 75 MHz value.
A zero firmware MIN fails before any BVB SET. An initially off BVB clock is valid before acquisition.
The BVB owner enables lazily and never sends SET_STATE(0), including release and destruction.
This mode does not map or access DVP, HDMI, PHY, reset or IRQ registers.
It does not establish power-domain ownership, pixel/PHY programming or KMS scanout.

A GET-only create failure with no BVB owner permits normal HSM cleanup.
After a failed acquisition, one cached native status can establish fault0, mutation0 and leases0.
Only that known unmutated state permits empty BVB destruction and normal HSM cleanup.
Cleanup does not turn the operation error into success.
Any uncertain status, SET outcome, native fault, failed release or failed destroy retains
both remaining kernel owners and the HSM lease. No retry, blind restoration or further firmware query follows.
Logs identify the retained stages and the validity of the last cached sample.
Cached fields do not establish the current physical clock state.

Retained clock callbacks and owner storage reside in the built-in kernel.
The provider stores no module callback or stack pointer.
A failed INIT can therefore remove module text, and FINI returns zero without a clock operation.
This differs from the IRQ mode's undrained callback retention. Use a controlled reboot for retained clocks.

## Source checks

```sh
HSM_PROBE_SKIP_MUTANT=1 sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
HSM_PROBE_FEATURES=on \
HSM_PROBE_MUTANTS='bvb-conflict bvb-hsm-floor bvb-hsm-verify bvb-minimum bvb-clean-error bvb-fault bvb-mutation bvb-lease bvb-held bvb-after-free bvb-initial-floor bvb-acquired-rate bvb-released-rate' \
    sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The fixture includes the actual module, HSM/DVP implementations, resource decoder and libfdt.
For BVB0, it controls the BVB API boundary. Kernel services and other mode boundaries
remain controlled as documented by the HSM probe guide.
The separate native-clock contract tests the actual BVB core and adapter.
The probe contract checks order, held HSM, status failures, known errors, retained uncertainty and no MMIO.
These are source checks. They do not claim a linked module, physical BVB control or HDMI output.
