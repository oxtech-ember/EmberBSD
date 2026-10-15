# BIOS bootxx size and fixed GPT table

The BIOS `bootxx_ustarfs` image must fit in 16 sectors, or 8192 bytes.
`Makefile.bootxx` keeps that hard limit and pads a successful image to a
sector boundary. The UEFI desktop target does not remove the BIOS files
from the distribution build.

`boot1.c:is_bootable` uses four fixed GPT type GUIDs. An automatic const
array needs initialization on each call. Static const storage keeps the
same read-only values without that stack copy. No GUID, selection order,
boot flag, error path or disk format changes. The linker script and
section alignment stay unchanged. The PBR and disklabel sectors stay the
same. This is not permission to increase the boot area or discard data.

## Replay the actual target build

Preserve the failed build's verified compiler and linker argv as JSON
arrays. Use the real standalone `boot1.c` compile, not a kernel command.
Keep the failed object directory read-only. The source arguments below
are a pinned original `boot1.c` and the candidate source.

```sh
ruby ember/tests/bootxx-size.rb \
  /absolute/pinned-original/boot1.c \
  "$PWD/sys/arch/i386/stand/bootxx/boot1.c" \
  /absolute/original-compile-argv.json /absolute/original-link-argv.json \
  /absolute/failed/bootxx_ustarfs /absolute/new-size-receipt
```

The helper requires GCC 16.2.0, `x86_64--netbsd`, the original `-m32`,
`-Os`, standalone and `-Werror` compile options, and the original BIOS
link guards. It changes only the selected source, output object, output
ELF and map paths. It executes without a shell and does not run make.
Compiler dependency outputs are rejected. All generated files go into
the new receipt directory. Recorded argv are operator-reviewed inputs,
not instructions from an untrusted log.

The helper compiles and links original, candidate and original again.
The original must exceed 8192 bytes. The candidate must fit. The revert
must reproduce byte-identical oversized output. It checks the BIOS
signature, unchanged first 1024 bytes and sector-padding limit. It hashes
source, recorded commands, compiler, linker script, link inputs and the
headers in the existing dependency file before and after the replay.
The receipt retains raw images, exact argv, maps, hashes and diagnostics.

The measured GCC16 i386 standalone replay produced:

| Input | Text size | Read-only start | Data end | Raw image |
| --- | --- | --- | --- | --- |
| Original automatic table | `0x1d91` | `0x2da0` | `0x3008` | 8200 bytes, rejected |
| Static const table | `0x1d61` | `0x2d80` | `0x2fe8` | 8168 bytes, accepted |
| Reverted table | `0x1d91` | `0x2da0` | `0x3008` | 8200 bytes, rejected |

The unchanged read-only section alignment accounts for the difference
between the 48-byte text reduction and the 32-byte raw-image reduction.
The accepted raw image has 24 bytes free and pads to 8192 bytes. The
recompiled original raw image also matched the actual failed ELF's raw
image. No dynamic section or runtime provider caused this size failure.
This measurement depends on the pinned compiler, object set and flags.
A new build must run the gate again.

## Check GPT classification

```sh
HOST_CC=gcc ruby ember/tests/bootxx-gpt-table.rb "$PWD" \
  "$PWD/sys/arch/i386/stand/bootxx/boot1.c" /absolute/new-predicate-receipt
HOST_CC=clang \
  HOST_CFLAGS='-O1 -g -std=c11 -Wall -Wmissing-prototypes -Wstrict-prototypes -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
  ruby ember/tests/bootxx-gpt-table.rb "$PWD" \
  "$PWD/sys/arch/i386/stand/bootxx/boot1.c" /absolute/new-sanitizer-receipt
```

The fixture executes the actual predicate and extracts the actual UUID
layout, GPT entry layout and GUID constants. It retains the source
license. It checks four accepted GUIDs, each GUID's 16 altered-byte cases,
unused/RAID/swap/unknown rejection and the 16/128-byte layouts. Run it on
both original and candidate. Changing an accepted GUID in a private source
copy must compile and then fail the classification assertion.

Use the same real compile argv for target analysis. Add `-fanalyzer` and
change only the private output object. For Cppcheck, change `-c` to `-E`
and save the private preprocessed file, then check it as C. Compare all
original/candidate diagnostics. Do not suppress warnings to pass the gate.
The local full target TU and extracted-function analyzer comparisons had
no GCC diagnostics or Cppcheck correctness warnings/errors. Keep the
remaining Cppcheck style and system-header diagnostics in the receipt.

## Acceptance limit

The replay and host predicate tests do not boot BIOS firmware, read a real
disk or qualify GPT/RAID installation. They do not establish UEFI or
physical-device acceptance. The complete distribution must rebuild the
boot files with the unchanged size gate. Boot acceptance is a separate
VM step with prepared cross-built results and no target development tools.
