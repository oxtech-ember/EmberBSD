# A133 diagnostic SDIO resource adaptation

The [FDT adapter](../sun50i_a133_fdt.c) translates only the inspected MMC1 resource
set. [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/ys-m33-a133/interfaces.md)
owns physical enumeration and radio support. Host enumeration is not WLAN support.

A zero-length root `ember,ys-m33-sdio-probe` property is required. Admission checks
the legacy MMC1 compatibility, 0x04021000/0x1000 register cells, SPI40 level-high
interrupt, four-bit bus, PG0..PG5 pins and enabled WLAN bus1. It validates the
WLAN R_PIO provider and PL5/PL6 cells without changing those GPIOs. Complete
compatibility strings, identity address mapping and the PIO/R_PIO/GIC binding
context must match. Alternate interrupt providers, card-detect/write-protect
GPIOs and a PIO `vcc-pg-supply` are rejected; pinctrl could otherwise enable an
unowned regulator. Unknown or disabled resources remain untranslated.

The conversion creates canonical `mmc1` pins, A100 bus/module clock IDs 67/63,
bus reset16 and the verified GIC parent. It selects non-removable, four-bit,
25 MHz operation. It removes UHS requests and legacy controller regulator
references so the diagnostic cannot select signaling voltage or write the PMIC.
It preserves regulator nodes, radio power, six-cell GPIO providers and unrelated
MMC2, touch, audio and MCU resources. There is no WLAN power sequence; an
unpowered radio may remain undiscovered.

Repeated opted-in fixups revalidate resources and are idempotent. Withdrawing
opt-in disables only a translated host with the adapter's ownership marker.
It must not disable an unrelated canonical host. A tight FDT buffer must report
failure rather than silently admit a partial translation.

From the OS root, `sh ember/tools/a133-sdio-fdt-test.sh` exercises production
adaptation with synthetic FDTs. It covers missing/malformed opt-in, foreign or
disabled resources/providers, repeated fixups, marker withdrawal and buffer
failure. Compilation and fixtures do not establish radio or physical recovery.

The separate [RTL8723DS diagnostic](../../../../dev/sdmmc/docs/rtl8723ds-probe.md)
owns CMD52 access and the optional function lifecycle.

## Provenance

Contracts consolidated from these documents at OS revision `4a4c98bb99999836103d3fe63fd6f4c98013f0c5`.
Git retains their original text; the hashes identify the inputs.

- [2026-10-08-a133-sdio-probe-design.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/specs/2026-10-08-a133-sdio-probe-design.md): SHA256 `bd7ba677df0c74847a2cff712b8172feee7eb76ff008e9dc8fe4eccdf893b8ad`.
