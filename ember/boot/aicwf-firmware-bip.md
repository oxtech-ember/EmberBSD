# Fixed-image AIC8800D80 BIP patch source

The [patch source](../tools/aicwf-firmware-bip.S) replaces two functions in
one external AIC8800D80 U02 firmware image. It adds a full 48-bit initial
IGTK receive floor, full receive packet-number comparison and a commit
after MIC validation. Reinstalling the same key keeps the transmit counter
and uses the greater receive floor. This is a source mechanism. Assembly
and host format checks do not establish VM, loader or radio acceptance.
The [host BIP helper](aicwf-bip.md) is a separate mechanism.

## Fixed input and outputs

The input is `fmacfw_8800d80_u02.bin` from
[the pinned external source](https://github.com/radxa-pkg/aic8800/blob/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/fw/aic8800D80/fmacfw_8800d80_u02.bin).
Its size is 341200 bytes. Its SHA256 is
`d2c7b7ac0bf8682c976c0c2da34f204c9b2f1ef8821252c6348a3016bd638f24`.
The `_h` image and other bytes with the same version number are unsupported.
The repository does not distribute original or modified vendor images.
The BSD-2-Clause identifier applies to the independent patch source and tools.
It does not state the license of an external firmware image.

| Section | Address | Exact output bytes | Original span SHA256 |
| --- | --- | --- | --- |
| `.aicwf_bip_key` | `0x142be0` | 340 | `b2712eebf753f9736e2dc26c513b77c9e12ee687a885099d1cf649671ba1294a` |
| `.aicwf_bip_rx` | `0x165e48` | 384 | `167300a59156fb0e39e41a7f0cdad5b3801f539899733b53f553332fcbe4fe4b` |

The [linker script](../tools/aicwf-firmware-bip.ld) refuses growth past either
span. No new BSS or code cave is used. The producer checks an ELF32 ARM
profile with exactly these two executable load segments. SDK's existing ELF
reader is ELF64; this finite ARM reader does not replace that interface.
The output contains two replacement files, a kernel data header and a manifest.
The header contains only the new bytes, digests and source provenance. The
output does not contain a full firmware image.

## Build

Select the common GCC16 ARM bare-metal toolchain and the SDK source explicitly.
The producer uses Python and its standard library. It checks SDK host API1,
uses its owned-process and path interfaces, and records the source/tool hashes.
The caller must own the output parent and keep inputs fixed during the build.
It refuses an existing output directory. Failure keeps incomplete outputs for
inspection. Do not reuse an incomplete result as an accepted patch.

```sh
python3 -B ember/tools/aicwf-firmware-bip.py \
    --sdk-source /absolute/EmberBSD-SDK \
    --toolchain /absolute/common-arm-gcc16 \
    --original /absolute/fmacfw_8800d80_u02.bin \
    --output /absolute/obj/probes/aicwf-firmware-bip-result
python3 -B ember/tools/aicwf-firmware-bip-test.py
```

`manifest.json` uses schema `aicwf-bip-patch-v2`. It records original and
replacement hashes, addresses, lengths, source hashes, compiler target and
commands. It also records `modified_sha256` and `kernel_header_sha256`.
The earlier v1 schema does not have these two fields. Successful construction
remains `assembled-not-executed`.

## Internal contract

The 44-byte key request has a versioned meaning only for cipher 5 and key length 16.
Bytes 8..23 hold the key. Bytes 24..29 hold the initial receive floor in LE48.
Bytes 30..39 must be zero. The existing cipher, VIF and pairwise fields remain.
The driver must require cipher 5, key length 16, KeyID 4 or 5, VIF 0..3 and
zero reserved bytes. It must validate the current association epoch. It must
send this extension only after an exact-image patched-load capability check.
This is an Ember extension. It is not an undocumented vendor API.

The key function compares all 16 backup key bytes and the valid/cipher/length
fields. A same-key request applies `max(current, initial)` to the receive floor.
It preserves the transmit counter, active pointer and key. A changed key is
invalidated before its fixed clear and initial-floor setup. Publication follows
key and metadata initialization. Generic cipher effects and the C ABI remain.

The receive function preserves existing non-BIP classification and return
semantics. The BIP path validates the key, parses LE32 plus LE16, rejects replay
and compares all eight MIC bytes. Failure does not commit a candidate floor.
Success commits once. Both BIP transactions preserve and restore the exact
previous PRIMASK value. A previous mask of 1 must never enable interrupts.
The patch does not modify the firmware's global nesting protocol.

The exact image runs key messages, VIF removal and normal RX as cooperative
KE callbacks. An existing masked channel-context path already calls the same
RX dispatcher and CMAC. The patch keeps one bounded BIP transaction within
that existing workload domain. This comparative source argument does not
measure silicon interrupt latency. The accepted CMAC path uses finite CPU
copy, fill, AES and subkey operations. It does not wait for IRQ completion.
The receive length remains bounded by the original u16 input domain; no
assumed radio-buffer length is substituted.

## WLAN loader contract

The [WLAN loader](../../sys/dev/sdmmc/if_aicwf.c) reads an external firmware
file into its owned RAM buffer. Only the exact non-H input hash selects the
extension. The loader replaces 724 bytes and checks the whole modified hash.
It does not change the external file. H and unknown images keep the legacy
path. They cannot send a nonzero initial IGTK IPN.

For the selected image, the loader checks the exact configuration, descriptor
and option-pair map. All four copy-block sizes must be zero. It reads all 181
replacement words after the option writes and before START_APP. No memory
write follows that final check. The dedicated path requires complete command
replies while command ownership is held. It validates each copied memory
address/data echo. BLOCK and START status words remain opaque diagnostics.

The private extension capability requires a complete 28-byte VERSION reply,
version `0x06090101`, MFP, a complete MAC/ADD_IF reply and VIF 0..3. Init must
complete before the capability becomes ready. Any selected-image mismatch
refuses attachment. An association epoch change does not remove an image
capability. Extended IGTK commands still require the current association and
successful key confirmation. Same-key requests reach the firmware maximum
operation; the host does not reset or skip its current replay state.

Run the native source contracts with an explicit SDK source and a new output
folder. OpenSSL 3 or later supplies the independent native digest provider.
The optional original file enables the exact-image and full loader cases.

```sh
python3 -B ember/tools/aicwf-fw-bip-contract.py \
    --sdk-source /absolute/EmberBSD-SDK \
    --original /absolute/fmacfw_8800d80_u02.bin \
    --output /absolute/obj/probes/aicwf-loader-check \
    --cflags='-O1 -g -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all'
```

These checks execute native projections of the actual loader. They do not
execute ARM instructions, start firmware or establish radio acceptance.

## Acceptance gates

Independent static decode and actual original/new Thumb regressions must check
all generic cipher effects, C ABI, PRIMASK 0/1 returns, initial floors, full 48-bit
boundaries, bad MIC, replay, duplicate-key maximum and VIF/key separation.
A VM is an instruction model. It does not qualify AIC radio behavior or timing.

A loader must verify the whole original hash, both original spans, both new
hashes and all write destinations. Vendor configuration and runtime patch
ranges must remain disjoint. It must publish capability only after complete
patched upload/readback and before the first extended key request. Cold reset
and association teardown must preserve the host epoch/confirmation contract.
Actual patched Thumb execution, boot-ROM upload and physical PMF/BIP checks
remain separate requirements.
No arbitrary firmware, model-wide compatibility or WPA3 certification is claimed.
