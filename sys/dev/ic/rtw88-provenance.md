<!-- Origin: EmberBSD native RTL8822C format research, AI-assisted. -->
# RTL8822C portable format contracts

These portable codecs validate one normal-firmware load profile,
physical EFUSE records, packet descriptors, and bounded DMA ranges.
It does not attach a device or enable a radio.
The firmware parser does not establish chip identity, authenticity, checksum validity,
or physical Wi-Fi operation. No kernel or firmware-loader integration exists yet.

## Driver reference and license

The reference is FreeBSD source revision
`a143197801c680b958ab662c88cf086668a1958b`, under
[`sys/contrib/dev/rtw88/`](https://github.com/freebsd/freebsd-src/tree/a143197801c680b958ab662c88cf086668a1958b/sys/contrib/dev/rtw88).
The relevant complete structures and functions were inspected:

- `fw.h`: `rtw_fw_hdr`, `FW_HDR_SIZE`, `FW_HDR_CHKSUM_SIZE`.
- `mac.c`: `check_firmware_size`, `start_download_firmware`,
  `download_firmware_to_mem`, and firmware download/checksum control.
- `main.c`: firmware-name selection, metadata extraction, and feature handling.
- `rtw8822c.c`: the complete `rtw8822c_hw_spec` initializer.

These files state `GPL-2.0 OR BSD-3-Clause` and Realtek's 2018-2019 copyright.
The native parser retains those notices and uses the BSD-3-Clause option.
See [the source license](rtw88-source.LICENSE).
The parser is a native adaptation of the format and size rules.
It does not import LinuxKPI or GPL-only implementation.
The separate shell/C tests are original BSD-2-Clause tooling.
OpenAI Codex provided AI assistance.

| Reference file | SHA256 |
| --- | --- |
| `fw.h` | `45a1bf5213eec95cd450a54bc50caa5652c959dd2b644684ec7447b278f6e701` |
| `mac.c` | `ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6` |
| `main.c` | `3c9104c5d96c439868a223c75da75609d8f239041356d40e71ea16169f69a301` |
| `rtw8822c.c` | `6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160` |

The RTL8822C device contract uses 48-byte TX packet descriptors,
16-byte TX buffer descriptors, 24-byte RX packet descriptors,
and 8-byte RX buffer descriptors. The normal firmware name is
`rtw88/rtw8822c_fw.bin`. These values come from `rtw8822c_hw_spec`.
They do not describe an implemented DMA mapping or PCI attachment.

## Firmware assets

Both fixtures come from the linux-firmware repository at revision
`681281e49fb6778831370e5d94e6e1d97f0752d6`.
The firmware README maps each name to the corresponding device.
WHENCE attributes the files to Realtek and selects
`LICENCE.rtlwifi_firmware.txt`.

| Role | Asset | Size | SHA256 |
| --- | --- | ---: | --- |
| Accepted profile, version 9.9.11 | [rtw88/rtw8822c_fw.bin](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6/rtw88/rtw8822c_fw.bin) | 202456 | `36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26` |
| Rejected load profile, RTL8822B | [rtw88/rtw8822b_fw.bin](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6/rtw88/rtw8822b_fw.bin) | 150984 | `378ff7b43ae7da18a0311175abc351a1758d25ce553b44f3c5694758efbea84c` |

| Metadata at the same revision | SHA256 |
| --- | --- |
| [LICENCE.rtlwifi_firmware.txt](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6/LICENCE.rtlwifi_firmware.txt) | `a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e` |
| [WHENCE](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6/WHENCE) | `6596bf833de5e775fa2e16596f5da2fe443b7990ddc491c116c3aadc89b1de51` |
| [rtw88/README](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6/rtw88/README) | `483fed8b35fadae40a539cdd619797a44dbcf8aa254b7298629771960e90630e` |

The firmware license is independent of the driver license.
It permits use and redistribution in binary form without modification,
subject to its notices and restrictions.
The complete notice is retained in [rtw88-firmware.LICENSE](rtw88-firmware.LICENSE).
Firmware binaries remain outside Git. Tests read complete, unmodified assets;
they do not execute or disassemble firmware.
Synthetic fixtures use protocol metadata and generated zero payload bytes.

Future image integration must bind the normal firmware to the accepted
asset SHA256 above and preserve its license. The parser does not verify hashes.
The test wrapper verifies both external asset hashes before it reads them.

## Supported layout

The little-endian header is 64 bytes. Relevant offsets follow `rtw_fw_hdr`:

| Offset | Width | Meaning |
| ---: | ---: | --- |
| 0 | 2 | Common signature `0x8822` |
| 4 | 2 | Firmware version |
| 6, 7 | 1 each | Subversion and subindex |
| 12 | 4 | Raw feature word; bit 0 indicates upstream feature validity |
| 24 | 1 | Memory usage; bit 4 enables EMEM |
| 28 | 2 | H2C format version |
| 32, 36 | 4 each | DMEM raw address and payload length |
| 48 | 4 | IMEM payload length |
| 52, 56 | 4 each | EMEM payload length and raw address |
| 60 | 4 | IMEM raw address |

Sections occur in file order DMEM, IMEM, EMEM, with no gaps or trailing bytes.
Each includes an eight-byte checksum trailer after its declared payload.
The parser uses subtraction before cursor additions and rejects 32-bit
payload-plus-checksum overflow. It does not read section contents.
It publishes no partial result on failure and accepts unaligned input.

The first bring-up accepts only the exact bases and payload lengths below.
The ranges include checksum trailers. These are fixture bounds, not claims
about the full silicon memory capacity. Raw address bit 31 is removed only
in the validated output, as in `start_download_firmware`.

| Section | File range, half-open | Raw header address | Load range, half-open | Payload bytes |
| --- | --- | --- | --- | ---: |
| DMEM | `[64, 19400)` | `0x80200000` | `[0x00200000, 0x00204b88)` | 19328 |
| IMEM | `[19400, 73472)` | `0x80030000` | `[0x00030000, 0x0003d338)` | 54064 |
| EMEM | `[73472, 202456)` | `0x80100000` | `[0x00100000, 0x0011f7d8)` | 128976 |

EMEM must be enabled. A new valid RTL8822C layout requires explicit profile
expansion and new evidence. Rejection of a future valid image is intentional.
The profile does not pin version or feature metadata; only the external
asset digest binds a particular firmware image.

The original requirement called for rejection of a wrong family.
Both real RTL8822B and RTL8822C assets use signature `0x8822`.
A distinct RTL8822C signature would be invented.
The contract therefore tests `unsupported_profile`: the real B asset passes
common structural checks but fails the supported destination/length profile.
An arbitrary image could copy that profile, so acceptance is not authentication.

## Reproduce the contract

Run synthetic bounds and profile tests without network access:

```sh
CC=gcc-16 sh ember/tools/rtw88-contract.sh
```

Fetch the pinned assets into an external directory. Keep their license there.
The Gitiles `format=TEXT` endpoint returns base64 data. On macOS:

```sh
fixture_dir=/absolute/external/rtw88-fixtures
mkdir -p "$fixture_dir"
base=https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/681281e49fb6778831370e5d94e6e1d97f0752d6
for name in rtw8822c_fw.bin rtw8822b_fw.bin; do
    curl -fLsS "$base/rtw88/$name?format=TEXT" -o "$fixture_dir/$name.base64"
    base64 -D < "$fixture_dir/$name.base64" > "$fixture_dir/$name"
done
curl -fLsS "$base/LICENCE.rtlwifi_firmware.txt?format=TEXT" \
    -o "$fixture_dir/license.base64"
base64 -D < "$fixture_dir/license.base64" > "$fixture_dir/LICENCE.rtlwifi_firmware.txt"
RTW88_FIXTURE_DIR="$fixture_dir" CC=gcc-16 sh ember/tools/rtw88-contract.sh
RTW88_FIXTURE_DIR="$fixture_dir" CC=gcc-16 \
    RTW88_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/rtw88-contract.sh
```

Use the host's equivalent base64 decoder on other systems.
This is a host contract of real parser source. Kernel integration, silicon
cut compatibility, firmware checksum hardware, PCI, DMA, VM execution,
and physical WPA2 acceptance remain separate work.

## Physical EFUSE and identity contract

`rtw8822c_efuse_decode` consumes physical WIFI-bank bytes, not a logical map.
It accepts a prefix of 1 through 512 bytes. The record region ends at
byte 388, before the 124-byte protected tail. The logical map is 768 bytes.
These sizes come from the complete `rtw8822c_hw_spec` initializer above.
The parser initializes the logical map to `0xff` and publishes it only
when every consumed record and the resulting PCIe MAC address are valid.
It performs no MMIO and has no bus callback.

The format follows the complete `rtw_dump_logical_efuse_map` in `efuse.c`.
Normal headers encode block in bits 7:4 and disabled words in bits 3:0.
A header whose low five bits equal `0x0f` has a second byte.
Its block is `((second & 0xf0) >> 1) | ((first >> 5) & 7)`.
The second low nibble disables words. Each enabled word supplies two bytes
at `block * 8 + word * 2`. Later records overwrite earlier words.
A first or extended second byte of `0xff` terminates the record stream.
An exhausted prefix is valid only between records, never inside a word/header.
Unlike the reference's permissive bounds, each read and write must fit
fully before the physical/logical boundary. The protected tail is never parsed.

The byte offsets follow the complete packed structures in `rtw8822c.h`
and `main.h`, plus `rtw8822c_read_efuse` and `rtw8822ce_efuse_parsing`:

| Logical offset | Decoded content |
| --- | --- |
| `0x10..0xb7` | Four 42-byte transmit-power calibration records, retained as raw bytes |
| `0xb8`, `0xb9` | Channel plan and crystal cap (low seven bits) |
| `0xc1`, `0xc3` | RF board option and Bluetooth setting |
| `0xc8` | Power tracking type (high nibble) |
| `0xca`, `0xcb..0xcc` | RFE option and country bytes |
| `0xd0..0xd1` | Path A/B thermal calibration |
| `0x120..0x125` | PCIe MAC address |

Multicast, all-zero and all-ones MACs fail; no random address is substituted.
The full logical map retains erased bytes and fields not yet interpreted.
No calibration defaults, signed-nibble interpretation, regulatory decision,
or protected-tail trim interpretation is performed. These remain PHY work.
The masked crystal cap and tracking type follow upstream decoding;
their unmasked values remain in the logical map.

`rtw8822c_validate_identity` checks observed PCI IDs, a `REG_SYS_CFG1`
(0x00f0) snapshot, and the decoded RFE option. This initial profile accepts
only `10ec:c822`; upstream also lists `c82f`, outside this target scope.
Register bits 15:12 encode the cut. Bit 23 selects test silicon when set.
Test silicon is rejected because no test-silicon profile is prepared.
RFE definitions 0 through 6 exist in `rtw8822c_rfe_defs`; other values fail.
Neither the EFUSE `rtl_id` nor the common firmware signature identifies
RTL8822C by itself. No inferred magic value is used as chip authentication.

Cuts A through G (0 through 6) are accepted as **source-format compatibility**.
The reference defines those encodings in `main.h`. Its `mac.h` maps each
cut to `1 << (cut + 1)`, stored in an eight-bit mask by `rtw_pwr_seq_parser`.
Encoding 7 or higher cannot select a step in that representation.
Rejecting them prevents silent omission of all power steps.
This is not proof that every listed cut exists or works on physical hardware.
Board revision A4 is unrelated to this silicon cut field.
The future hardware owner must handle failed register reads and apply these
checks before configuration; a successful pure validator cannot prove a read.

## Pinned configuration inventory

The additional files below use the same FreeBSD revision and Realtek
`GPL-2.0 OR BSD-3-Clause` license recorded above. The BSD option applies.
The complete functions and structures relevant to EFUSE, identity,
condition matching, RFE selection and power execution were inspected.
Configuration arrays are inventoried, not imported or executed here.

| Reference file | SHA256 |
| --- | --- |
| `efuse.c` | `2120e85547a1f622a5248eeeffd96e068d552d326809bc64488b8c84b21daff5` |
| `efuse.h` | `9a7ca402cd372484c180d8b046da53e2d320e6bbad32243456dd6119b2a71f15` |
| `rtw8822c.h` | `975cb88c627ebabb93729c6b5082a6274a9fe69a11094830db83e95486e93b18` |
| `main.h` | `bab159780b164959b5bd981d24544507558ed816334d49fd1a31d4d17a69285e` |
| `reg.h` | `c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324` |
| `mac.h` | `006c62b0d7b7a6dee95cc27172c0452bcfa6b05f80a1a91610c4750f42f4c91a` |
| `phy.h` | `55388bdbec3b743553776282b918d2f6ffe06d4ac1d8849cf0ea0e22f19867e4` |
| `phy.c` | `669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec` |
| `rtw8822ce.c` | `4959bfa9e81fb55d2a554ec69ed93819ad700fcfee1bf1ea542625d1b38b7987` |
| `rtw8822c_table.c` | `ec3f4d9eed016932b9100e4e2332c12f67bd0824a3f3b4b05cc515e7916a2247` |
| `rtw8822c_table.h` | `9c52f30bfcf62815da793c19ce5cdb8e9acaa5d789c9929cf8a74178670b445c` |

`rtw8822c_table.c` is 1,106,662 bytes and 46,105 lines.
The pinned arrays are imported as `sys/dev/ic/rtw8822c_table.c` with a native
descriptor wrapper. The original hash above is of the FreeBSD source file;
the wrapper does not change array contents. `rtw8822c_boot.c` walks MAC, BB,
AGC, RFK, RF B then A, power-by-rate type 0 and power-limit type 0 or 5 by RFE
before `rtw88_start` sets the RF-enabled flag. Normal table loading does not
enter DPK mode. The DPK tables remain available for a separate calibration
transaction with its required backup and restore operations.
Host contract `ember/tools/rtw88-start-contract.sh` is the executor check.
`ember/tools/rtw8822c-init-contract.sh` compares the actual native table-load
call graph against the unchanged pinned `rtw_phy_load_tables` producer.
That contract records table dispatch and RFK mask writes, not analog behavior.
The startup contract also checks real decoded AFE registers before ADCK.
No partial substitute or table from a different chip is provided.
The complete table inventory at that exact revision is:

| Table suffix after `rtw8822c_` | Source declaration/body lines | Purpose |
| --- | --- | --- |
| `mac` | 10-11 | Empty upstream MAC table; do not invent register writes |
| `agc` | 15-1856 | Conditional AGC register/data pairs |
| `bb` | 1860-3301 | Conditional baseband pairs and delay markers |
| `bb_pg_type0` | 3305-3352 | Band/path/TX-count/address/mask/data power-by-rate rows |
| `rf_a` | 3356-21352 | RF path A pairs |
| `rf_b` | 21356-39820 | RF path B pairs |
| `txpwr_lmt_type0` | 39824-42165 | Regulatory/band/bandwidth/rate/channel/signed power limit |
| `txpwr_lmt_type5` | 42169-43535 | RFE 5 power limits |
| `dpk_afe_no_dpk` | 43539-43575 | DPK AFE setup |
| `dpk_afe_is_dpk` | 43579-43619 | DPK AFE setup |
| `dpk_mac_bb` | 43623-43638 | DPK MAC/BB setup |
| `array_mp_cal_init` | 43642-46103 | RF calibration initialization |

The complete power transitions are in `rtw8822c.c`, lines 4584-4865:
card-disabled to card-emulation, card-emulation to active, active to
card-emulation, and card-emulation to card-disabled, in that order.
Enable runs the first two; disable runs the last two.
Each row carries offset, cut mask, interface mask, base, command, mask/value.
Commands 0-4 mean read, write, poll, delay and end. Polling must be bounded.
The upstream bound is 20,000 attempts at 50 microseconds, with one PCI retry.
PCI selection uses interface bit 2; its PHY condition uses interface value 1.
Those are distinct encodings and must not be interchanged.

The future executor must preserve these semantics from `phy.c`/`main.h`:

- Conditional pairs use bits 31/30 for positive/negative markers and
  bits 29:28 for IF/ELIF/ELSE/ENDIF. Cut occupies bits 27:24, package
  bits 15:12, interface bits 11:8, and RFE bits 7:0. Zero cut/package/
  interface is a wildcard; RFE must match exactly for this chip.
  Driver cut A maps to condition value 15. Package zero also maps to 15.
- MAC writes are 8-bit; AGC/BB writes are 32-bit. BB pseudo-addresses
  `0xfe..0xf9` mean 50 ms, 5 ms, 1 ms, 50-60 us, 5 us, 1 us delays.
  RF `0xffe` means 50 ms; `0xfe` means 100-110 us. Other RF writes
  use the 20-bit mask and a one-microsecond following delay.
- `rtw8822c_hw_spec.rf_tbl` is ordered B then A. Each table carries its
  own path in `RTW_DECL_TABLE_RF_RADIO`; preserve that explicit path.
- `rtw_phy_load_tables` orders MAC, BB, AGC, optional BTG AGC, RFK, RF.
  RFK includes register preparation in `rtw_load_rfk_table` before its table.
  RFE 0-6 use power-by-rate type 0. Only RFE 5 uses power-limit type 5;
  the other definitions use type 0. Power tracking tables and physical
  protected-tail trim reads remain separate calibration dependencies.

A future table import must preserve complete arrays, license notices,
source revision, conditions, delays, limits and RF-path attribution.
It requires bounded parsing tests and failure propagation before hardware
use. This inventory is preparation, not acceptance of an executable PHY.

## Packet descriptors and DMA aperture

The codecs use the same pinned FreeBSD revision and BSD-3-Clause option.
`tx.h` and the complete `rtw_tx_fill_tx_desc` in `tx.c` define TX fields.
`rx.h` and the complete `rtw_rx_query_rx_desc` in `rx.c` define RX fields.
`pci.h` defines buffer descriptors; `pci.c` defines their actual consumers.
These six files retain Realtek's dual-license notice and 2018-2019 copyright.
The additional USB/SDIO files below establish checksum call sites only.
No USB, SDIO, LinuxKPI, or WLAN implementation is imported.

| Reference file | SHA256 |
| --- | --- |
| `tx.h` | `5f0c1dc2c8e9dbcdfece4e34c264bd24731ab6a3ff28011f9ddb09cb695aaa46` |
| `tx.c` | `d598fe4d658a9c03de9f0468f0bd7f07b50232f7732fd0f49ed6367221aa3978` |
| `rx.h` | `789e4fb6baecc2c6e7ac4d802d2ed2b32b799f47b3c37abd81daccdda1b13afa` |
| `rx.c` | `8f877d9ca9061b5783e3513c85024370ebebb233f7b40acae88fee496636dae9` |
| `pci.h` | `49129551f28352e1edd567199261893a84f4d451d3fec3af04909085b41c219d` |
| `pci.c` | `c84aca85002a049c07f4ecad9394be680b148580a008e01b01e0363266023ee1` |
| `usb.c` | `c26b679e17f3b016fb363a75a915a36404c155eae24caae8f015fd1fa2506db3` |
| `sdio.c` | `07dd361d4e36599716e580839b7ed88cfc480083082105e2bfbcd53d793f0306` |

`usb.c` has Realtek's dual-license notice. `sdio.c` has the same license
with 2021 Martin Blumenstingl/Jernej Skrabec and Realtek attribution.
They are reference-only files, not adapted implementation.

### TX fixed48 profile

`rtw88_tx_encode` writes exactly 48 bytes, even if capacity is larger.
It sets OFFSET to 48, LS to one, and PKT_OFFSET to zero.
The caller supplies a nonzero 16-bit packet size and bounded raw fields.
Queues 0-15 are TIDs; 16-19 are beacon, high, management, and H2C.
The high queue sets MORE_DATA, as in `rtw_tx_fill_tx_desc`.
RTS selects hardware rate code 8 (24 Mbps) and RTS_SHORT.
RTL8822C has `old_datarate_fb_limit=false`; that field remains zero.
The public header lists each supported semantic flag and raw field.
Out-of-width fields fail instead of being silently masked.
Byte stores permit unaligned destinations and make the format host-independent.

| Byte offset | Contents |
| ---: | --- |
| 0 | Packet size, descriptor offset, BMC, LS, DISQSELSEQ |
| 4 | MAC ID, queue, rate ID, security, PKT_OFFSET, MORE_DATA |
| 8 | Aggregation, report, density, BT null |
| 12 | HW SSN selection, rate/RTS/NAV controls, maximum aggregation |
| 16 | Data rate, RTS rate |
| 20 | Short GI, bandwidth, LDPC, STBC, RTS short |
| 24 | 12-bit SW_DEFINE/report sequence |
| 28 | Checksum in low 16 bits; DMA aggregation remains zero |
| 32 | Hardware sequence enable |
| 36 | Software sequence and optional TIM insertion offset/enable |
| 40, 44 | Zero reserved words |

The complete `rtw8822c_fill_txdesc_checksum` computes
`(packet_offset * 8 + 48) / 2` halfwords. `fill_txdesc_checksum_common`
first clears w7 low16, XORs those little-endian halfwords, then stores
that XOR in w7 low16. This codec accepts only packet_offset zero,
so it covers all 24 halfwords. It never uses the older 32-byte rule.
The literal contract fixture includes nonzero words after byte 31;
its checksum is `0x23eb`. A 32-byte checksum mutant fails that test.

The pinned PCI TX path does **not** call the checksum helper.
USB and SDIO paths do, including USB reserved-page transmission.
This codec emits the documented chip checksum as a format operation.
It does not claim PCI requires it or that checksum checking is enabled.
The future PCI consumer must select its evidenced transport behavior;
no DMA descriptor is submitted by this codec.
Variable packet offsets, USB aggregation, and custom reserved words need
explicit extensions before use. A nonzero packet_offset returns ENOTSUP.
All other null, capacity, and field errors return EINVAL.
Every error leaves the destination unchanged.

### RX buffer bounds

`rtw88_rx_decode` takes the received buffer starting with the 24-byte
packet descriptor. It decodes packet length from w0 bits 13:0,
driver-info size from bits 19:16 in eight-byte units, and shift from
bits 25:24. Driver info starts at `24 + shift`; payload follows it.
Both the metadata prefix and payload must fit the supplied buffer length.
Trailing buffer capacity is allowed. Byte loads accept unaligned input.

CRC/ICV error, PHY status, SWDEC and C2H remain explicit flags.
The codec also returns raw security encoding, MAC ID, PPDU count,
rate, bandwidth, and low TSF. It does not turn these into decryption,
authentication, PHY, or 802.11 frame-validity decisions.
A zero packet length can be structurally valid; consumers must validate
frame or command semantics before delivery. C2H never bypasses bounds.
Null, short, and out-of-buffer input return EINVAL without changing output.

### DMA address contract

The supported native PCI profile is **32-bit DMA addressing**.
This is derived from the complete buffer layouts and programming path,
not from a FreeBSD memory-allocation workaround:

- `rtw_pci_tx_buffer_desc` is eight bytes: size16, PSB length16, DMA32.
  Each 16-byte TX buffer descriptor contains two such segments.
  `rtw_pci_tx_write_data` fills segment zero with the 48-byte packet
  descriptor and segment one with the payload at `dma + 48`.
- `rtw_pci_rx_buffer_desc` is eight bytes: size16, total-size/tag16, DMA32.
  Both `rtw_pci_reset_rx_desc` and `rtw_pci_sync_rx_desc_device`
  publish only the low 32-bit address field.
- `rtw_pci_reset_buf_desc` programs every TX/RX ring base with write32.
  No high-address programming path is established by these functions.
- FreeBSD-only `dma_set_mask(32)` and `dma_set_coherent_mask(32)` in
  `rtw_pci_io_mapping` corroborate this profile but do not establish
  the full physical silicon's maximum address capability.

`RTW8822C_DMA_ADDR_BITS` is 32. `rtw88_dma_validate` accepts apertures
1 through 32 bits and a nonempty byte range. Every byte must fit.
The check uses `len - 1 <= maximum - addr` after checking `addr`.
It never forms an overflowing `addr + len` or truncates an address.
Address `0xffffffff` with length one is valid for the 32-bit aperture;
length two, addresses above 4 GiB, and wrapping ranges fail EOVERFLOW.
Widths 33-64 return ENOTSUP; zero length or width outside 1-64 is EINVAL.
These errors deliberately expose unsupported requests.

Future bus_dma integration must allocate/map within the selected aperture
and validate entire rings and all packet segments before publication.
Failure to obtain a suitable mapping must fail visibly. IOMMU mappings
are bus addresses; a host physical-memory size is not proof of an aperture.
No mapping, alignment, coherency, ring publication, register write, or
physical DMA operation is validated by this pure range helper.
A proven wider hardware mode needs new buffer formats and PCI programming
before this restriction can be relaxed.

For only descriptor/range tests, run
`RTW88_DESCRIPTORS_ONLY=1 CC=gcc-16 sh ember/tools/rtw88-contract.sh`.
Omit that selection for firmware/EFUSE/identity regression and real fixtures.

## Native PCI resource attachment

`if_rtw88_pci.c` and `rtw88.c` implement quiescent resource attachment for
PCI 10ec:c822. This stage creates no network interface. It does not load
firmware, publish ring addresses, unmask interrupts, or enable bus mastering.
Select `rtw88* at pci? dev ? function ?` in an explicit kernel configuration.
The default board configuration is unchanged.

The pinned `pci.c` and `pci.h` above establish BAR2, eight TX queues and one
RX queue. BK/VI/VO/management/high/H2C have 128 buffer descriptors, BE has
256, beacon has one, and RX has 512. TX entries are 16 bytes; RX entries
are eight bytes. The native allocator requests 256-byte alignment (a
conservative allocation constraint), one coherent DMA segment and a
mandatory 32-bit subregion tag. It validates the full loaded bus range,
size, alignment and segment count before ownership transfer. There is no
fallback to an unconstrained DMA tag, address truncation, or IOMMU change.
Packet buffers and descriptor execution remain separate integration work.

`rtw_pci_request_irq` requests one MSI or legacy INTx, without MSI-X.
The native attachment uses the same alternatives through `pci_intr_alloc`.
`rtw_pci_disable_interrupt` establishes HIMR0=0x00b0, HIMR1=0x00b8 and,
for the RTL8822C WCPU, HIMR3=0x10b8. Each mask is cleared and read back
before allocating rings or installing the passive handler. The handler
returns unclaimed for shared INTx and never touches MMIO. It must be
replaced with validated status processing before enabling interrupts.

The shared core owns complete ring mappings and the established interrupt.
The PCI attachment owns BAR mapping, restricted DMA tag, native mutex and
PCI command restoration. Failed partial allocations unwind locally. Core
teardown removes the interrupt before freeing rings; PCI teardown then
releases the mutex, tag and BAR. Command restoration never re-enables a
bus master inherited from firmware. PCI status W1C bits are not replayed.
After disestablishment, `xc_barrier(0)` waits for pending IPL_NET handlers
on every CPU before vector, ring or mutex release. The driver holds no
mutex across this wait. The native low-priority xcall wait drops BKL when
it sleeps, allowing an already selected non-MPSAFE handler to finish.
The native PIC still owns its internal interrupt-source lifetime. The
fake-bus contract proves driver ordering, not physical SMP behavior.

Register reads return status separately from data and preserve output on
failure. Bounds, alignment and PCI presence are checked. Presence is checked
again after an access, so an observed removal is not ordinary register data.
Native `bus_space` supplies no error result and cannot catch a synchronous
CPU bus fault here. A valid all-ones register value is not rejected by itself.
Critical mask writes have readback; ordinary writes only check presence and
issue a bus-space write barrier. No successful EFUSE or silicon read is
claimed by this resource attachment. Later bootstrap must propagate both
hardware-read and parser failures before any configuration sequence.

Run `sh ember/tools/rtw88-pci-contract.sh` for actual PCI/core source with
fake native bus APIs. It tests each acquisition failure, complete unwind,
32-bit range crossing, map geometry, masked IRQ lifetime and device removal.
The existing firmware/EFUSE/descriptor contract remains independently runnable.

## Office bootstrap on 2026-10-16

`hw.rtwce_bootstrap` is the operator trigger. Write 1 calls `rtw88_start`;
write 0 calls `rtw88_stop`. Attach still does not start the radio. A missing
firmware image returns before bus master is enabled.

DDMA reads the chip TX buffer at `0x18780000+48`, not the host mapping.
`rtw88_start` publishes the allocated empty TX/RX rings, sets PCI control
bits 15 and 20, then posts each firmware chunk on the one-entry beacon ring
and kicks `0x383` bit 4. Internal DDMA runs only after that descriptor's
OWN bit clears. The rings are not kicked for data. Stop clears the published
base addresses and bus master. Host results after this change: start
contract 76/0, PCI contract 5339/0.

The office Orange Pi 6 Plus used the diagnostic RAM root and FFS `dk1`.
The accepted rollback image was not modified. Pinned firmware
`rtw8822c_fw.bin` was copied to `/libdata/firmware/rtwce/` and checked:
202456 bytes, SHA256
`36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26`.
The RAM root drops that file on reboot. FFS keeps `netbsd.7c29a5ba`,
`netbsd.a0f81da8` and `netbsd.task5-boot`.

| Image | BUILDINFO | Size | SHA256 | Silicon |
| --- | --- | ---: | --- | --- |
| task5-boot | `EmberBSD:task5-boot` | 17895496 | `6c1a467f2336fb549e14475fcbabcf563f9051a2f80a908ab75c96485b8bc423` | booted; `rtwce0` quiescent; bootstrap returned ETIMEDOUT in about 1.6s with no stage line |
| task5-fwpost | `EmberBSD:task5-fwpost` | 17895672 | `0c3aaa1a2836c50f63a5b0ef86f60eabcea138e582ce5aafcbe45a593f09d139` | booted as EMBER64 #3; same quiescent attach |
| task5-dma | `EmberBSD:task5-dma` | 17895752 | `d6a9b8ac44a6ccde25a8024086f4f76a9d9ba8ebf5390d731c5cc2def839a2f2` | built only; not installed and not booted |

`task5-fwpost` is the kernel left on `dk1`. Its dmesg line is
`rtwce: firmware beacon queue timed out` at 61.8576046s. There was no
power-poll line, so identity and the power sequence returned before the
beacon wait. Internal DDMA did not run. Wi-Fi is not up.

The Mac USB Ethernet service stays manual at `192.168.2.1/24`, but the
address can leave `en7` until the service is toggled. The board uses
`rge1` at `192.168.2.8/24`. DHCP on the board times out when no server is
offered.
