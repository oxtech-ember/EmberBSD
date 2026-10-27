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

## Native WLAN resource gate

The native `sun50i-a133-ys-m33.dts` describes MMC1 at `/soc/mmc@4021000`.
It uses PG0..PG5, A100 clock IDs 67/63, reset 16 and SPI40 level-high.
The bus is four-bit and limited to 25 MHz. The controller stays disabled.

PL5/PL6 resource names do not establish GPIO polarity or power timing.
No power sequence, regulator, signal voltage or pin drive strength is guessed.
Enabling this controller requires a separate verified board power profile.
That profile must use standard `mmc-pwrseq` and define GPIO and regulator ownership.
Revision-specific schematics and matching vendor source must establish polarity and delays.
The diagnostic root opt-in does not enable the native controller.
The [native bootstrap](../../../../dev/sdmmc/docs/rtw88-sdio-bootstrap.md)
also checks this admission boundary before scheduling firmware work.

From the OS root, run `python3.15 ember/tools/a133-wlan-fdt-test.py SOURCE OUTPUT`
with the SDK Python. The script compiles the actual board DTB. Select `HOST_CC`
and `HOST_DTC` explicitly from the native SDK prefix.
Use `HOST_CFLAGS` and `HOST_LDFLAGS` for that prefix's libfdt inputs.
The test calls the production FDT adapter and checks the disabled resource gate.
It also checks that diagnostic opt-in and a foreign power reference cannot activate MMC1.
The adapter must preserve the complete native tree, including its existing MMC2 resources.
The separate synthetic test above retains coverage for legacy touch and MCU resources.

These checks prove a resource model, not radio I/O or a cold boot.
This native DTS has no complete touch or MCU description and is not a deployment replacement.

## Provenance

Contracts consolidated from these documents at OS revision `4a4c98bb99999836103d3fe63fd6f4c98013f0c5`.
Git retains their original text; the hashes identify the inputs.

- [2026-10-08-a133-sdio-probe-design.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/specs/2026-10-08-a133-sdio-probe-design.md): SHA256 `bd7ba677df0c74847a2cff712b8172feee7eb76ff008e9dc8fe4eccdf893b8ad`.
