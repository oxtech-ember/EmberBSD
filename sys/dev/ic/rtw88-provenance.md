<!-- Origin: EmberBSD native RTL8822C format research, AI-assisted. -->
# RTL8822C firmware container contract

This portable parser validates one normal-firmware load profile.
It does not attach a device or enable a radio.
It does not establish chip identity, firmware authenticity, checksum validity,
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
They do not describe an implemented DMA or PCI attachment.

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
