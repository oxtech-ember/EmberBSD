# AIC8800D80 U02 instruction fixture

This tool prepares and runs a selected Cortex-M4 instruction fixture. It does
not boot the FullMAC firmware or model AIC radio, DMA, scheduling or timing.
It uses only the external original U02 image from
[`radxa-pkg/aic8800` at `d13d07963cd15d731e2895e8288a04cca6152ac9`](https://github.com/radxa-pkg/aic8800/blob/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/fw/aic8800D80/fmacfw_8800d80_u02.bin).
The exact 341200-byte input must have SHA256
`d2c7b7ac0bf8682c976c0c2da34f204c9b2f1ef8821252c6348a3016bd638f24`.
The H image and other revisions are rejected.

No vendor bytes are stored here. The fixture extracts nine selected slices
into a new private output. It retains the original function addresses,
literals and helper tables. It never loads the original boot vectors or the
whole firmware. A strict ELF load-segment check keeps fixture data in its
own RAM area before CPU execution. Keep the generated slices, ELF and observations outside Git.
The original binary's license and source obligations remain separate from
this independently authored BSD-2-Clause fixture. AI assistance was used.

## Inputs and commands

Run the host-only checks with Python and its standard library:

```sh
python3 -I -B ember/tools/aicwf-firmware-check/test-host.py
```

Prepare external slices and synthetic test cases without a compiler or VM:

```sh
python3 -I -B ember/tools/aicwf-firmware-check.py \
    --firmware /absolute/fmacfw_8800d80_u02.bin \
    --output /absolute/new-private-output
```

The `build` and `run` modes require the shared
[Ports firmware C frontend](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/cross/firmware.md)
and an explicit [SDK API1 source](https://github.com/oxtech-ember/EmberBSD-SDK).
Each invocation requires a new output directory. `run` prepares and builds
before it starts QEMU. It does not reuse a previous output:

```sh
python3 -I -B ember/tools/aicwf-firmware-check.py --mode build \
    --firmware /absolute/fmacfw_8800d80_u02.bin \
    --compiler-prefix /absolute/arm-gcc16/toolchain \
    --sdk-source /absolute/EmberBSD-SDK --output /absolute/new-build-output
```

For a separately reviewed execution, select `--mode run` and add
`--qemu /absolute/qemu-system-arm --timeout 120`. The timeout bounds CPU
observation. Build producers use the SDK's ordinary owned logging.
The model is `mps2-an386` with TCG and no disks or network backend.
A private Unix GDB socket controls the model. Only non-modifying TCG
breakpoints are requested; instruction bytes are checked before and after.
Unknown process closure retains its marker and control namespace.

## Actual-code boundary

The C driver calls the original key leaf at `0x142be0`, RX verifier at
`0x165e48` and CMAC at `0x16651c`. Original AES, subkey, tables, copy, fill,
frame classifier and MMIE finder instructions execute too. They are not
replaced with a successful CMAC service. Two external boundaries are explicit:

- The generic cipher0/3 random helper at `0x153d84` returns a fixed fixture
  value and increments a call counter. This does not test that generator.
- The assertion target at `0x156520` traps. It cannot return success.

The synthetic VIF table and MPDU inputs are fixture state, not device
snapshots. Results retain complete before/after bytes for two VIFs, return
registers, callee-save checks, PRIMASK, special output and helper call counts.
The Python oracle follows RFC4493 independently of the vendor tables. Known
AES and CMAC vectors test that oracle before instruction acceptance.
Expected original defects are recorded separately from desired security
requirements: nonzero initial floors are lost, received PN is truncated,
bad MIC can advance the floor, and same-key installation resets state.

An accepted original fixture means that those exact original observations
were reproduced. `accepted_secure_firmware` stays false. The original profile accepts no
replacement function input. The finite patch profile below has separate input
admission and a desired-state oracle. Full instruction-boundary IRQ injection,
cooperative ownership, reset/rekey lifetime and silicon IRQ latency remain
separate gates. The provided model IRQ and PRIMASK observations do not close
them. QEMU time does not measure AIC silicon cycles.

The [original acceptance receipt](original-acceptance.json) records 33 matched
cases and 14 original-defect witnesses on QEMU 11.1.2 with GCC16.2.0.
It binds the source, input, ELF and raw-result hashes. Own C passed Cppcheck,
GCC `-fanalyzer` and Clang Static Analyzer; Clang produced no target objects.
This is not WPA3/PMF certification or approval to replace device firmware.

## Reviewed patch inputs

The default mode still uses the original 33 cases. Add both `--patch-elf` and
`--patch-manifest` to select the finite BIP patch fixture. The consumer accepts
only the exact reviewed v1/v2 ELF identities. It checks the original image,
manifest, assembly/linker identities, two RX load segments and section hashes
before it creates output. Tool and producer SHA256 values are recorded as
provenance; they do not restrict the host to one compiler binary. V1 manifests
have the original exact field set. V2 adds modified-image and kernel-header
SHA256 values. The modified-image hash is checked in memory against the
original plus both replacements before output creation. Only key340/RX384 replace fixture slices. Seven
original slices remain unchanged. No patch ELF is loaded over the vectors.

```sh
python3 -I -B ember/tools/aicwf-firmware-check.py --mode build \
    --firmware /absolute/fmacfw_8800d80_u02.bin \
    --patch-elf /absolute/reviewed/bip.elf \
    --patch-manifest /absolute/reviewed/manifest.json \
    --compiler-prefix /absolute/arm-gcc16/toolchain \
    --sdk-source /absolute/EmberBSD-SDK --output /absolute/new-patch-output
python3 -I -B ember/tools/aicwf-firmware-check/test-patch.py
python3 -I -B ember/tools/aicwf-firmware-check/test-irq.py
```

The patch profile has 146 finite cases. Its independent desired-state oracle
checks the full deterministic before/after VIF bytes, full 48-bit floor,
new/same-key state, TX and active-pointer preservation, current HW index,
ABI, PRIMASK, helper calls, return values and complete observed MPDU effects.
The original CMAC/AES instructions still execute. Expected MIC bytes come from
the independent host oracle. V1 is a causal input: the same desired comparator
must refuse its stale same-key HW index. It is not rejected by a forced
version-dependent result. Host tests are not instruction execution evidence.

The runner saves expected, pre-execution and post-execution hashes for all
nine loaded slices. Patch acceptance remains CPU-fixture acceptance only.
`accepted_secure_firmware` and `device_or_radio_acceptance` remain false.
Use `--exercise irq` with the reviewed v2 inputs to select 36 bounded model
scenarios after separate source, opcode and build review. Six operations use
before/during/after pending-IRQ injection with input PRIMASK0/1. Only IRQ31's
model NVIC pending bit is written. Read-only handler snapshots retain the
selected context and active pointers. The observer checks stacked exception
PCs, pending delivery, the returned mask and full snapshots. Input PRIMASK1 must
hold its pending IRQ across eight own NOPs before the harness explicitly
releases its caller mask. That release is not selected-firmware behavior.

The controller uses hardware breakpoints and `continue`; it does not issue
single-step packets. QEMU's default step mask suppresses IRQs. Any future
single-step extension must set and read back ENABLE1 without NOIRQ/NOTIMER
before it can provide IRQ evidence. Unsupported remote operations refuse.
A completed bounded fixture sets `accepted_bounded_irq_fixture`, while
`irq_interleavings_qualified` and `full_instruction_coverage` remain false.
It does not establish all interrupt boundaries, AIC scheduling or timing.

The explicit `--causal-early-enable` flag admits only one separately compiled
negative payload identity. Its two CPSID instructions become CPSIE. The
identical IRQ observer must refuse delivery inside a protected interval.
A version-dependent forced refusal does not establish this causal result.
The negative input is never a normal accepted patch profile.
