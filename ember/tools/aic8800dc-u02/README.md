# Verify the Luckfox Lyra Zero W WLAN files

These host tools identify the factory AIC8800DC USB U02 station files for the
Luckfox Lyra Zero W. Use the original WLAN driver and firmware directory from
one image. The supported sets are the factory NAND release with patch build
`g0a86431` and the 261020 release with patch build `g09cea8d`.

On the matched onboard radio, the kernel reads these six files through
firmload after the root filesystem mounts and all 17 clock and power steps
succeed. Install them in `/libdata/firmware/aic8800dc`. The kernel checks
the release digest and table boundaries. It reports the release, WLAN file
size and patch table row count. This stage reads host files only. The
passive station can attach if the files are unavailable or invalid.

On the matched Lyra, the driver also samples the radio's DPD marker before
WLAN setup. It checks that the normal-mode RF descriptor selects the known
U02 header. The factory driver reported the same location on a
[Pico Ultra W](https://github.com/LuckfoxTECH/luckfox-pico/issues/131).
The Lyra driver then reads the three mask words and the reserved word.
`present` and `absent` report the factory marker in this sample.
`unknown` means the descriptor differed or a read failed. The station
attaches in passive mode if its USB transport remains available.

## Prepare the tables

Use the Lyra 261020 SDK from [Luckfox downloads](https://wiki.luckfox.com/Luckfox-Lyra/Download/).
The SDK pins `external/rkwifibt` to Git commit
`223f24790d1ba8f96d35d19367ab933f81f82aa7`. The inputs are
`aicwf_compat_8800dc.c` and `rwnx_main.c` under
`drivers/aic8800/aic8800/aic8800_fdrv` in that repository.
The tool checks both source digests before it invokes the selected SDK Clang
and LLVM objcopy. It emits the eleven factory arrays in ARM little-endian
order. The same 4,836-byte file applies to both releases.

Run the commands from the EmberBSD source root. Set `SDK_BIN` to the EmberBSD
SDK executable directory and `RKWIFIBT` to the extracted driver source root.

```sh
python3 ember/tools/aic8800dc-u02-tables.py \
    --bsp "$RKWIFIBT/drivers/aic8800/aic8800/aic8800_fdrv" \
    --sdk-bin "$SDK_BIN" aic8800dc_u02_tables.bin
```

The expected SHA-256 of the output is
`7e934020dd15a26c162628132e59db8746ec9e23ef2086027071303a16b97025`.

## Check an image

Set `FDRV` to the original `aic8800_fdrv.ko` from the selected image.
Set `FW_DIR` to its extracted firmware directory. It must contain
`aic_powerlimit_8800dc.txt`, `aic_userconfig_8800dc.txt`,
`fmacfw_calib_8800dc_u02.bin`, `fmacfw_patch_8800dc_u02.bin`, and
`fmacfw_patch_tbl_8800dc_u02.bin`.

```sh
python3 ember/tools/aic8800dc-u02-kit.py \
    --driver "$FDRV" --firmware-dir "$FW_DIR" --tables aic8800dc_u02_tables.bin
```

A match prints the release name, driver SHA-256, and station kit SHA-256.
Missing files, changed bytes, and mixed releases cause a nonzero exit.
This check establishes the identity of host files. Radio calibration and
firmware readiness require separate observations on the device.

The non-H U02 patch tables start with a 128-byte header. Each remaining row
contains two 32-bit words. The NAND table is 896 bytes and has 96 rows.
The 261020 table is 1,008 bytes and has 110 rows. The factory driver sends
the header before the rows and prints the description from its file buffer.
A matching build string can remain from an earlier run or an incomplete upload.
The loader replaces the return code after each row. A later success can hide
an earlier error. Check the descriptor, the first row error and the count of
accepted rows to verify the full transfer.
[Factory patch table loader](https://github.com/EliSofke/LuckfoxLyraSDK/blob/6dcbca0d8a517d79bd8fb4205ca4966b136604bc/external/rkwifibt/drivers/aic8800/aic8800/aic8800_fdrv/aicwf_compat_8800dc.c#L1713-L1768)

The YuzukiHD Zephyr component contains three non-H U02 arrays for the WLAN
patch, patch table and calibration program. In little-endian byte order,
all three match the NAND files. All three differ from the 261020 files.
The component defaults to SDIO. Its USB option depends on `BROKEN`.
[Zephyr firmware arrays](https://github.com/YuzukiHD/zephyr-components/blob/ed8886e9e29d5e49d01a6774cae916b71f0bf50a/aic8800/host/drv/fw/aic_fw.c#L192-L220) ·
[Transport selection](https://github.com/YuzukiHD/zephyr-components/blob/ed8886e9e29d5e49d01a6774cae916b71f0bf50a/aic8800/Kconfig#L83-L96)

## Read startup conditions on Lyra

At USB attach, `uaicfw` reports the initial clock selector for the matched
DC U02 station. The labels `factory first` and `factory final` mean that
the selector matches the first or second factory setting. The driver reports
zero separately. A different value gives `other`. An unavailable reading
gives `unknown`. The PMU mask label comes from the chip identification
reply. The factory driver samples the selector, then applies both clock
settings. Clock frequency and completion of power setup require separate
checks.

### Configure the onboard clock and PMU

On the Lyra Zero W, the onboard radio is on port 1 of the CH334F on
RK3506 OTG1. The driver identifies the USB function, radio revision and
FullMAC interface before setup. The supported cold state has chip 0x07,
sub-ID 0x01, USB setup skipped, no CPU crystal, a zero clock selector and
the extended PMU mask. Other initial conditions keep the passive path.

For this state, the driver applies both factory clock settings in order,
then the 15 masked settings from the non-H DCDC table. The two factory
driver modules contain identical tables. The factory header defines each
confirmation as two 32-bit words. The native driver checks both lengths,
the echoed register and the selected settings. Three divider writes also
set a DENOM_UPDATE bit. The driver checks the divider in the reply
and accepts either value of the update request bit. The factory host driver checks
the confirmation ID. Its boot log does not report the confirmation data.

A failure stops attachment. The log identifies the step and reports
length or echo errors. A missed confirmation leaves the effect of the
last command unknown. Rebinding the onboard radio during that boot keeps
it stopped. Completion confirms the command sequence. Supply voltage,
PLL lock, WLAN patch loading, DPD and station operation need separate
checks.

## How the factory station uses the files

On USB DC U02, the normal station driver loads the WLAN patch and configures
its tables before starting the FullMAC stack. Both factory drivers use
`CONFIG_DPD` and `CONFIG_FORCE_DPD_CALIB`. The latter still permits reuse.
The driver first checks the digital predistortion (DPD) result in radio RAM.
It reuses a valid result. Otherwise it loads `fmacfw_calib_8800dc_u02.bin`
and runs calibration on that radio. The file contains a calibration program.
The resulting coefficients belong to the individual radio.

In these two modules, only RF test mode calls `aicwf_dpd_result_apply_8800dc`.
The normal station path has no DPD result file to install. The later
`MM_SET_RF_CALIB` request is a separate operation after stack start.
When comparing drivers, check the early DPD path separately from this request.
[Factory startup](https://github.com/EliSofke/LuckfoxLyraSDK/blob/6dcbca0d8a517d79bd8fb4205ca4966b136604bc/external/rkwifibt/drivers/aic8800/aic8800/aic8800_fdrv/rwnx_platform.c#L1630-L1653) ·
[DPD check and calibration](https://github.com/EliSofke/LuckfoxLyraSDK/blob/6dcbca0d8a517d79bd8fb4205ca4966b136604bc/external/rkwifibt/drivers/aic8800/aic8800/aic8800_fdrv/aicwf_compat_8800dc.c#L2445-L2537)

## Compare USB station drivers

The USB driver in the UGREEN V1.6 tree assigns 88dc to the FullMAC driver.
Its station initialization loads the WLAN patch, applies its configuration
and calls early DPD calibration. The WLAN patch, patch table and calibration
program in that tree match the Luckfox 261020 files byte for byte.
Both text configuration files differ. Keep the files from one verified kit.
[USB binding](https://github.com/oliveagle/ugreen_aic880dc_wifi_usb/blob/691a41d1c3ca80e44bd35f0fe33f2cdc2f1f5f72/drivers/aic8800/aic_load_fw/aicwf_usb.c#L1855-L1866) ·
[Station initialization](https://github.com/oliveagle/ugreen_aic880dc_wifi_usb/blob/691a41d1c3ca80e44bd35f0fe33f2cdc2f1f5f72/drivers/aic8800/aic8800_fdrv/rwnx_platform.c#L1605-L1627)

The Lyra Ultra W Yocto recipe selects a separate Linux driver revision.
Its firmware directory also has the three matching 261020 binaries.
The selected driver loads the patch and issues the later RF calibration
request. Its source omits the early U02 DPD helper used on the Zero W.
[Yocto recipe and driver revision](https://github.com/OOHehir/luckfox-lyra-ultra-yocto/blob/b37253cb8c17ec4530235fe211ab806d8db177c5/meta-rockchip-rk3506/recipes-connectivity/aic8800dc/aic8800dc-wifi-bt_1.0.bb#L1-L18) ·
[RF calibration in that revision](https://github.com/Kiborgik/aic8800dc-linux-patched/blob/ef97a943cde794131160f4591538297e3f5fe1a8/drivers/aic8800/aic8800_fdrv/aicwf_compat_8800dc.c#L1930-L1965)

K230 RT-Smart implements the same early check for an existing DPD result.
The host driver checks the DPD state in the radio again after calibration.
If the result is still invalid, RT-Smart stops startup. Use the files that
match the tested release and check that the station can scan and join.
[RT-Smart DPD result checks](https://github.com/canmv-k230/rtsmart/blob/eae08a28e47b01c984ed0c4421e66358a7295de6/kernel/bsp/maix3/drivers/extdrv/aic8800/aic8800_firmware.c#L2929-L3007)

## License

The EmberBSD host tools use the BSD-2-Clause license. The generated table
file contains data from the vendor source files. The original authors retain
their rights. Apply the terms of the corresponding upstream distribution
when using or redistributing those data and the original firmware files.
