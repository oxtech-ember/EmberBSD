# A733 PowerVR firmware metadata validation

The native [parser](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c)
checks PowerVR firmware metadata before a future MIPS loader uses it.
It performs no allocation, file access, MMIO, DMA, or firmware execution.
It does not validate ELF contents, executable instructions, signatures, or
firmware authenticity. Successful metadata validation is not a GPU driver.

## Interface and limits

Include [sun60i_a733_gpu_fwinfo.h](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.h).
Call `sun60i_a733_gpu_fwinfo_parse(bytes, size, expected_bvnc, &info)`.
Supply a stable byte buffer and the packed BVNC observed on the target GPU.
The caller must limit file allocation before calling the parser.
The parser reads little-endian fields explicitly and accepts unaligned buffers.
It returns positive errno values. Every failure leaves `info` unchanged.

The first implementation accepts:

- A file whose size is a multiple of 4096 bytes, with a final 4096-byte info block.
- Info version 3, a 48-byte header, and 1 through 8 entries of 24 bytes each.
- Firmware major version 1, the open-source compatibility flag, and 4096-byte firmware pages.
- A nonempty payload before bounded device metadata, including its four u64-counted arrays.
- The MIPS processor feature, without META or RISC-V processor features.
- Known BRN, ERN, and feature bits from the pinned definitions below.

The compatibility flag describes the firmware interface. It does not change
the firmware's separate redistribution license.
Unknown formats, processors, features, or extra parameter values return
`EOPNOTSUPP`. Malformed ranges and missing parameters return `EINVAL`.
A BVNC mismatch returns `ENODEV`. Device-info padding need not be consumed.

MIPS layout IDs retain their upstream wire values:

| ID | Meaning | Allocation type |
| --- | --- | --- |
| 4 | Main code | CODE, 1 |
| 5 | Exception code | CODE, 1 |
| 6 | Boot code | CODE, 1 |
| 7 | Private data | DATA, 2 |
| 8 | Boot data | DATA, 2 |
| 9 | Stack | DATA, 2 |

Each actual allocation must be nonempty and fit its declared maximum.
Address ranges, allocation offsets, and per-type size sums must not wrap.
Duplicate IDs and overlapping actual firmware-address ranges are rejected.
Allocation offsets must not overlap within one type or exceed that type's sum.
CODE and DATA use separate buffers, so their offsets can overlap.
`max_size` does not reserve address space beyond `alloc_size`.
A valid subset of sections is accepted as metadata.
Required boot sections and ELF `PT_LOAD` compatibility are not checked.
This acceptance does not mean that firmware is ready to load or execute.

The result contains decoded layouts, per-type allocation sizes, version fields,
BRN/ERN masks, feature masks, and feature values. Boolean features have value 1.
Parameter features use the upstream ascending-feature-ID order.
`sun60i_a733_gpu_fwinfo_feature()` returns `ENOENT` for an absent capability;
a present parameter with value zero still succeeds.
The result owns no input pointers. Array offsets and counts identify bounded
spans in the original buffer, which the caller must keep unchanged when using them.

## Native runtime preflight

After [runtime stages 1 through 11](a733-gpu-identification.md#runtime-staged-diagnostics-with-a-watchdog-bound)
have identified the real GPU, stage 12 reads the file through the kernel's
`firmload` interface. It limits file allocation to 4096 through 1048576 bytes.
It closes the file and frees the buffer before publishing `fw_state`.
Open, size, allocation, read, parse and close errors remain visible.
The stage does not transfer bytes to the GPU or change device registers.

Obtain the pinned firmware and its license from the provenance links below.
On the board, verify their hashes before installing both files as root:

```sh
sha256 rogue_36.56.104.183_v1.fw LICENSE.powervr
install -d /libdata/firmware/powervr
install -m 0444 rogue_36.56.104.183_v1.fw LICENSE.powervr \
    /libdata/firmware/powervr/
sysctl -w hw.sun60igpuid0.stage=12
sysctl hw.sun60igpuid0.result hw.sun60igpuid0.fw_state
```

Compare both hashes with the values below before running `install`.
This path is part of the standard firmware search path. No path sysctl
change is needed. A successful result starts with `metadata only error=0`.
Stage 12 is single-use per boot. Stage 0 cannot restart a failed sequence.
Normal observe-only boot does not open the firmware file.

## Physical preflight result

On 2026-10-12, a 4 GiB Orange Pi Zero 3W booted GCC16.2 `EMBER64 #4`
from `a7bbb4ed91b99cdeafa8a9232ac9839a2855c544` with four matching modules.
Board revision was not recorded. Vendor boot files and the existing
observe-only DTB were preserved. The NPU identity probe completed first.
All twelve explicit GPU runtime stages returned zero. The hardware watchdog
covered each stage and did not reset the board. The stage sequence completed
within one second in the timestamped transcript.

```text
metadata only error=0 version=1.1.6976702 bvnc=0x00240038006800b7
layouts=6 code=102400 data=20480 payload=122880
```

Both installed firmware files match the pinned hashes below.
Kernel ELF SHA256: `94ac9937aa77c228fe1441bd6b3f7be19b9cad45e23a49bca6c53544f1c42f62`.
Native image SHA256: `1ba57e2a325aed359d81108bbd87b477fe03da87862142db3874e8a15bb1e0d6`.
Preserved DTB SHA256: `6e3e92e251a617066a54634394e65d3463df96fdd5920753056cb0c2e4e78d4e`.
The normal boot leaves GPU execution disabled. This result validates file
loading and metadata compatibility, not firmware execution, DMA, rendering,
HDMI output, or accelerated Plasma.

## Reproduce the checks


The [contract](../tools/a733-gpu-fwinfo-contract.sh) compiles the production
parser directly. It has no synthetic implementation of the parser.
Run it from the source root:

```sh
sh ember/tools/a733-gpu-fwinfo-contract.sh
CC=clang FWINFO_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-fwinfo-contract.sh
```

To include the external firmware, pass its local path as the single argument.
The script first checks the pinned SHA256 below. It neither downloads the file
nor modifies it. Mutation cases use project-owned synthetic metadata only.
The tests cover truncation, unaligned input, count arithmetic, layout overlaps,
unsupported processors/features, parameter ordering, and unchanged output on failure.
They include 20000 deterministic mutations of synthetic metadata.

The 2026-10-12 host run passed 76990 checks with the pinned firmware and ASan/UBSan.
Apple Clang 21 Static Analyzer and Cppcheck 2.22 reported no diagnostics for the
parser and contract. GCC 16.2 compiled the parser with the actual EmberBSD
AArch64 kernel headers and configuration; its parser stack frame was 1008 bytes.
Target Clang Static Analyzer and Cppcheck also reported no diagnostics.
GCC `-fanalyzer -Wanalyzer-too-complex` exhausted its graph budget in the
device-info loops. A larger graph budget did not complete that analysis.
These are recorded analysis limits, not a clean GCC analyzer result.
Valgrind does not support the macOS/arm64 host used here; ASan/UBSan was used instead.
These checks do not establish physical firmware execution or long-run GPU operation.

## Provenance and licensing

The format and feature parameter mapping come from the Linux PowerVR driver,
revision `7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
The source files offer `GPL-2.0-only OR MIT`; this adaptation selects MIT.
The native parser preserves Imagination Technologies' 2023 attribution and
includes the MIT notice. EmberBSD's parser and tests were developed with AI assistance.

| Upstream file | SHA256 |
| --- | --- |
| [pvr_fw_info.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_info.h) | `d79454e2cb1e2dfc8e8a12ce6d455e6d18e7d9c0b33b31359ed913ee6fe187a6` |
| [pvr_fw.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw.c) | `c55b994aa005f727f6708879162c83f86d8c35201f2cda036795cbd39470daea` |
| [pvr_rogue_fwif_dev_info.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_dev_info.h) | `d3d9c39a514a0b90dcd8756e9058e89e059fc51c19670766ed30d9d0cc4c84a5` |
| [pvr_device_info.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_device_info.c) | `8fa613f77f42c651135fa208a74c0d68ec01f827976bf895ef277139a0fc6ac2` |
| [pvr_device_info.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_device_info.h) | `a1ee656dab1e405d1cc10fbf08ac1168eb59e42d3ffc6d1945abc6aa2db0413b` |

The separate test input is Imagination's
[`powervr/rogue_36.56.104.183_v1.fw`](https://gitlab.freedesktop.org/imagination/linux-firmware/-/blob/8a58f81883f7be458daa34e418cc4079f995b279/powervr/rogue_36.56.104.183_v1.fw),
revision `8a58f81883f7be458daa34e418cc4079f995b279`, Git blob
`ba5466bd6d085d7a9745d877e90dc77018d149d1`, size 131072 bytes.
Its SHA256 is `1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2`.
Its metadata identifies firmware 1.1, build 6976702, with six layouts and
BRN/ERN/feature-mask/parameter counts of 1/1/2/29.
The decoded CODE/DATA sizes are 102400/20480 bytes.

The firmware is not MIT-licensed and is not included in this source change.
Its separate [LICENSE.powervr](https://gitlab.freedesktop.org/imagination/linux-firmware/-/blob/8a58f81883f7be458daa34e418cc4079f995b279/LICENSE.powervr)
permits binary redistribution under its stated conditions.
The recorded license SHA256 is `1c9aa6bd6703a7ce1cdb879542fa1d8aca115a327bd819b193c971de9c53f402`.
The tests read documented metadata; they do not disassemble or change executable contents.
