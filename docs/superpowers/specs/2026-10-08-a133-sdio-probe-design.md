# YS-M33 opt-in SDIO host probe

The next autonomous night step is discovery of the tablet's Wi-Fi bus.
Physical audio, microphone recording and custom boot firmware remain excluded.
The running audio kernel identifies the vendor MMC1 node as not configured;
the existing resource adapter only translates MMC2 for eMMC. A WLAN network
driver is a separate deliverable. Host enumeration alone must not be described
as working Wi-Fi or association.

Use the existing A100 MMC host driver with a narrowly guarded FDT translation.
Only a zero-length root `ember,ys-m33-sdio-probe` property opts in. Validate the
inspected MMC1 legacy compatibility, 0x04021000/0x1000 register cells, SPI40
level-high interrupt, four-bit bus, legacy PG0..PG5 pin group and enabled WLAN
bus 1. Validate the WLAN's existing R_PIO provider and PL5/PL6 resource cells;
do not change those GPIOs. Validate complete compatibility strings, identity
address mapping and the inspected PIO/R_PIO/GIC register and binding context.
Reject alternate interrupt providers, card-detect/write-protect GPIOs and a
PIO `vcc-pg-supply`; applying pinctrl can otherwise enable that regulator. Unknown or disabled resources remain untranslated.

Create canonical `mmc1` pins, use A100 bus/module clock IDs 67/63 and bus reset
16, copy the verified GIC parent and limit the bus to 25 MHz. Mark it
non-removable. Remove UHS mode requests and legacy regulator references from
the controller so this diagnostic cannot select signalling voltage or write
the PMIC. Preserve the regulator nodes, radio power state, six-cell GPIO
providers, MMC2, touch, audio and MCU resources. No WLAN power sequence is
introduced. A powered-down module may therefore remain undiscovered.

Keep an ownership marker on the translated host. Repeated opted-in fixups must
be idempotent and revalidate resources. Removing opt-in disables an owned
translated host, preventing stale probe resources from silently attaching.
Never disable an unrelated canonical host without that marker.

Test the production adapter with synthetic FDTs before implementation. Cover
missing opt-in, wrong resource cells, disabled nodes, foreign pin/provider
references, repeated fixups, marker withdrawal and tight-buffer failure.
Build a clean source pin natively. If resource review and build pass, test the
container from RAM with UART recovery and the previous eMMC boot unchanged.
Observe host/card identification and retain GUI/Ethernet/MCU/default-off audio.
Do not install the diagnostic kernel or infer radio support from compilation.
