# Frozen EmberBSD HSM reference

`bcm2712-hsm-reference.c` is the exact production source from EmberBSD commit
`6e548fea09ffc04cb844c0d55c799ce25173a74b`, path
`sys/arch/arm/broadcom/bcm2712_display_hsm.c`.
Its SHA256 is `8c07499213bc7e1dcba43ddce092618994edc90fd3722de121afad9079223bb4`.
The source has the BSD-2-Clause license shown in the file.

This file is a test-only wire and error oracle. No kernel configuration selects it.
The clock runner verifies its hash, removes kernel includes and the RCS declaration,
and renames only its public function symbols with preprocessor definitions.
The test compares the common engine and HSM facade with this independent reference.
Do not update the reference when either implementation changes.
