# Arrow Lake HX laptop touchpad

Arrow Lake HX laptops, such as the MSI Vector 17 HX AI (MS-17S3, Intel
Core Ultra 9 275HX), use the 800 Series PCH-S (Meteor Point-S). Their
touchpad is a HID-over-I2C device with `_HID CUST0001` and `_CID PNP0C50`.
It reaches the system through two PCH functions:

- an LPSS I2C controller, PCI `8086:7f4c` on the MS-17S3, driven by `dwiic(4)`;
- a `GpioInt` on the PCH GPIO controller `INTC1084`, driven by `igpio(4)`.

On that laptop the firmware selects GPIO interrupt mode for the touchpad,
not an APIC interrupt. `ihidev(4)` therefore needs `igpio(4)` before
`ims(4)` and `wsmouse(4)` can attach.

## What the kernel matches

`dwiic(4)` matches all six PCH-S LPSS I2C functions listed by Linux
`intel-lpss-pci.c`: `0x7f4c`-`0x7f4f`, `0x7f7a` and `0x7f7b`.

`igpio(4)` matches `INTC1084` with the layout from Linux
`pinctrl-meteorpoint.c`: five communities, `HOSTSW_OWN` at `0x150`,
`GPI_IS` at `0x200` and `GPI_IE` at `0x220`. ACPI GPIO numbers on this
controller start each pad group at a 32-aligned base. For such controllers
`igpio(4)` uses the ACPI numbers as gpio pin numbers and translates each
one to its community, pad and GPI register. Numbers between groups have
no pad. Older controllers keep their linear pad numbering.

For a translated pin, interrupt establishment follows Linux:

- pads still owned by ACPI are refused, because they signal GPEs;
- the pad is set to GPIO input with all direct routes cleared;
- the trigger is programmed and the group `GPI_IE` bit is set;
- attach masks and clears every group, so only established pins interrupt;
- `ihidev(4)` level interrupts use the controller's mask and unmask.

`igpio_acpi` registers such a controller as the ACPI GPIO provider, so
`ihidev(4)` can resolve the touchpad `GpioInt` through `acpi_gpio`. It
also establishes the controller interrupt when the device has no `_AEI`,
as on the MS-17S3. Controllers with linear numbering are not registered:
their pad numbers may not match firmware GPIO numbers.

The SoC GPIO controller `INTC1082` is not matched; the touchpad does not use it.

## Checks

```sh
CC=gcc-16 sh ember/tools/igpio-mtps-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' sh ember/tools/igpio-mtps-contract.sh
```

The contract compiles the unmodified `igpio.c` and `igpio_acpi.c` against
modeled registers and ACPI services, with ACPICA types. It checks the PCI
and ACPI IDs, numbering holes, translation at group and community
boundaries, ACPI ownership, trigger setup, dispatch, acknowledge,
mask/unmask from inside a handler, disestablish, `GpioInt` translation,
the missing `_AEI` path and failed attach unwinding. It also covers older
`igpio(4)` defects: disestablish on a non-zero bank, detach after a bank
without setup, a second detach after a failed attach, and releasing an
interrupt that was never established.

## Validation status

Source, host contract and a GCC16 amd64 `GENERIC` kernel build only.
The firmware values were read from one MS-17S3: I2C0 enabled, touchpad
at address `0x15`, GPIO interrupt mode. The new kernel has not been booted
on that laptop yet; touchpad input on hardware remains unverified.
