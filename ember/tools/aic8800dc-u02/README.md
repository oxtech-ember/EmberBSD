# Verify the Luckfox Lyra Zero W WLAN files

These host tools identify the factory AIC8800DC USB U02 station files for the
Luckfox Lyra Zero W. Use the original WLAN driver and firmware directory from
one image. The supported sets are the factory NAND release with patch build
`g0a86431` and the 261020 release with patch build `g09cea8d`.

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


## License

The EmberBSD host tools use the BSD-2-Clause license. The generated table
file contains data from the vendor source files. The original authors retain
their rights. Apply the terms of the corresponding upstream distribution
when using or redistributing those data and the original firmware files.
