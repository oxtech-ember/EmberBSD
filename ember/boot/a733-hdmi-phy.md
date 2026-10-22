# A733 HDMI PHY core

The core programs the A733 TOP PLL and SNPS C422-0 PHY through checked callbacks.
It does not use the older C082 PHY recipe.
It does not attach hardware or start a TCON.

The native caller must serialize all calls.
It must retain the same display owner before the first register write.
The `ready` callback must prove the current retained owner, clocks and supplies.
It must not write hardware.
The read and write callbacks must check the register aperture and access width.
The core retains the first error.
After an error, it makes no more register accesses or delay calls.
It has no retry, reset or automatic teardown operation.
Keep resources after a write attempt until a proven stop or reboot.

## Clock admission

Use the physical DCXO frequency.
Do not substitute the normalized 24 MHz system clock.
The core admits these exact fixed-table outputs:

| DCXO | Pixel clock |
| --- | --- |
| 24 MHz | 27, 54, 74.25, 148.5 MHz |
| 26 MHz | 54, 108 MHz |

The core checks the complete integer ratio before it returns a plan.
It rejects an unsupported clock and leaves the plan output unchanged.
The native caller must select the mode from validated sink EDID.
A supported PLL frequency does not establish sink support.
The initial profile uses RGB888 with no pixel repetition.

## Register phases

`sun60i_a733_hdmi_hpd_prepare` selects the HDMI pad and PHY clock route.
It enables HPD sensing and masks PHY interrupt delivery.
It keeps the transmitter off with `PDDQ=1` and `TXPWRON=0`.
It rejects external PHY control and test mode before its first write.
It does not read EDID.
The native caller uses the existing checked DW-HDMI DDC controller for that step.

`sun60i_a733_hdmi_phy_prepare` selects one admitted clock plan.
It checks HPD before programming the PLL.
It checks TOP lock for at most 20 ms of requested delay.
It applies the C422-0 reset sequence: assert with one, then deassert with zero.
The older GEN1 polarity is not valid for this profile.
It writes the three MPLL values and three drive values through PHY I2C.
It reads each value back and compares all 16 bits.
The one exception is bit 15 of VLEVCTRL (`0x0e`).
The vendor drive table sets this bit, and the core writes it unchanged.
The core does not compare it, because the PHY does not return it.
The A733 PHY returns `0x0160` after a write of `0x8160`.
Public field maps define only `sup_ck_lvl[4:0]` and `sup_tx_lvl[9:5]` in this register.
Bit 15 is a defined override field only in other registers, for example CKSYMTXCTRL (`0x09`).
The core keeps the full comparison for `0x09` and all other registers.
Neither the vendor driver nor the Linux DW-HDMI driver reads these registers back.
The final PHY lock check stays the functional gate after the table writes.
It uses register `0x11` for the third MPLL value.
It does not use the older PHY register `0x15`.
Each PHY I2C operation has at most 5 ms of requested delay.
The final PHY lock check has at most 2.5 ms of requested delay.
An I2C error takes priority over a done flag.
A timeout does not authorize cleanup writes.

These delay bounds do not bound a stalled bus transaction.
`sun60i_a733_hdmi_phy_observe` checks the selected PLL fields, both locks and HPD.
It does not establish that the sink displays an image.
The native caller owns voltage, CCU, SYS_CFG, EDID, VO1 and TV0 policy.

## Cached comparison record

The core keeps the last PHY I2C comparison in initialized storage.
The native coordinator exposes it as read-only `hw.sun60ide0.phy_trace`.
This is a separate 28-byte record. It does not change `run_trace` or `output_trace`.
It contains seven native `uint32_t` words in this order:
`version`, `valid`, `index`, `address`, `requested`, `observed`, `status`.
Version zero means no comparison request was recorded. All words are then zero.
Version one uses these `valid` masks:

| Mask | Valid fields or event |
| --- | --- |
| 1 | Zero-based table index, internal PHY address and requested 16-bit value |
| 2 | Last successful read of the PHY I2C status register |
| 4 | Complete observed 16-bit PHY value |
| 8 | The masked comparison ran |

The table address order is `0x06`, `0x10`, `0x11`, `0x19`, `0x0e`, `0x09`.
The record uses values from existing operations. It adds no register access or delay.
An error retains the partial record and prevents a later table entry from replacing it.
The optional typed getter copies this cache under the output provider mutex.
It remains available after a sticky output error. It performs no readiness query or I/O.
A status value without mask 2 is not a measured status.
An observed value without mask 4 is not a measured PHY value.
These diagnostics do not change the comparison or establish visible HDMI output.

## Checks

Run the actual C core and the independent host fixture:

```sh
python3 -B ember/tools/a733-hdmi-phy-contract.py
```

Set `CC`, `A733_HDMI_CFLAGS` or `TEST_RUNNER` for host checks.
`A733_HDMI_SOURCE_ROOT` selects a source tree for causal controls.
The test checks known 108 MHz values, exact ratios, unrelated bit retention,
GEN2 reset polarity, I2C errors, timeouts and failure at each I/O operation.
It also checks that a missing VLEVCTRL bit 15 passes, and that a bit 15 error
at `0x06`, `0x19` or `0x09` and a bit 14 error at `0x0e` fail.
The checks remain active with `-DNDEBUG`.
These tests do not establish physical HDMI, DDC or pixel accuracy.
ASan/UBSan execution is not Valgrind acceptance.

## Hardware sources

Register fields and fixed values come from the following primary BSP files.
The source pin is `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`.
The implementation is independent BSD C.
The GPL files supply hardware facts; their implementation is not copied.

- [A733 TOP PHY fields](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_hdmi20/phy_top.h)
- [DCXO tables and TOP PLL sequence](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_hdmi20/phy_top.c)
- [C422-0 MPLL and drive values](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_hdmi20/phy_snps.c)
- [GEN2 reset polarity](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_hdmi20/dw_mc.h)
- [PHY I2C, HPD and power fields](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_hdmi20/dw_phy.h)
- [Linux DW-HDMI PHY register names](https://github.com/torvalds/linux/blob/7be4bc52fd9e0e55dc9a2e0232b89427d2c4b614/drivers/gpu/drm/bridge/synopsys/dw-hdmi.h)
  and its write-only [PHY configuration](https://github.com/torvalds/linux/blob/7be4bc52fd9e0e55dc9a2e0232b89427d2c4b614/drivers/gpu/drm/bridge/synopsys/dw-hdmi.c)
- [VLEVCTRL and CKSYMTXCTRL field decode](https://chromium.googlesource.com/chromiumos/platform/drm-tests/+/f806cb7d7f14dd17087ae8ebed67bd3589708812/rk3288_hdmitables.py)
  for the related DWC HDMI 3D TX PHY; NXP application note AN4671 gives the same VLEVCTRL fields

The broader [native output boundary](a733-output-timing.md) remains separate.
