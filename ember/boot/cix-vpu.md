# CIX P1 H.265 codec foundations

Status: checked source foundations. The hardware driver is incomplete.
Origin: EmberBSD contributors, AI-assisted.

EmberBSD has no attached CIX P1 video codec driver or M2M device.
These helpers do not load firmware into the device or encode frames.
The target is Orange Pi 6 Plus with CIX CD8180 and stock ACPI firmware.
The OS owns firmware, DMA, interrupts, power/reset and the application API.
Ports owns FFmpeg and GStreamer consumers.

## Implemented scope

The independent BSD source in `sys/dev/ic` implements bounded wire operations:

| Source | Contract | Remaining caller responsibility |
|---|---|---|
| `cixvpu_fw.[ch]` | Validate the protocol 3 firmware header, text extent, BSS bitmap and shared-region layout. Reject invalid input without publishing partial output. | Select an authorized blob, allocate pages and load/boot firmware. Structural validity is not authentication. |
| `cixvpu_ring.[ch]` | Pack and unpack bounded message rings, including the selected `-sum` firmware's asymmetric checksum framing. | Own coherent snapshots, locking, DMA synchronization, barriers and index publication. |
| `cixvpu_mmu.[ch]` | Encode 40-bit bus addresses and checked two-level page entries. Reject invalid permissions, occupied entries and stale removals. | Allocate/map DMA memory, stop the device and manage caches/TLBs. Removal is not safe during active DMA. |

These files are not selected by a kernel configuration or device attachment.
`sys/videoio.h` adds the standard `V4L2_PIX_FMT_HEVC` fourcc without changing
structure layout or ioctl numbers. The existing `video(4)` driver remains
capture-only; the presence of M2M declarations does not implement two queues.

The selected `-sum` protocol adds a cumulative checksum word between the TX
header and payload. That word is outside the header's payload size.
RX messages have no inline checksum. Their validation uses the entire committed
snapshot, prior consumed total and three published cumulative totals.
The helpers preserve checksum state and output on malformed input or insufficient space.
They do not emulate firmware or publish device-visible indices.

## Source and firmware provenance

The OS baseline is `7fac11898f3451472b6288d84ea3fdc87ca4f808`.
The maintained reference is Sky1 Linux 6.18 LTS at
`57e018a398248d7e5e4d798610df79a557c0629f`, principally its CIX codec patch.
The retired DKMS repository is historical evidence only.
The helpers are original BSD implementations of functional wire formats.
No GPL implementation was imported or relabelled. Arm China/CIX reference
copyright and licence terms remain with their original sources.

The locally inspected firmware is Sky1-Linux/sky1-firmware at
`dd81690747ddb092bcc2a221daa0f8f679ee4bc4`, `firmware/hevcenc.fwb`:

```text
SHA256 2f745394cc8700233448d74cf4fb14d18eba547d2259de5e0bbf825a1efef56d
size 420736 bytes; protocol 3.5; version r0p0-sum0005
text 103 pages; BSS 34 pages; shared 6 pages; private 9 pages
```

The parser accepts this actual image's layout. It does not execute the image.
Upstream describes proprietary redistributable firmware, but the applicable
individual licence has not been established. No blob is included in this
repository or authorized for a new package by these source checks.

## Reproduce the source checks

From the repository root, use GCC 16.2.0 on a supported build host:

```sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-fw-test.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-ring-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-mmu-contract.sh
```

The firmware script prints its build directory and retains its CLI and fixtures.
Run `cixvpu-fw-check /absolute/path/to/hevcenc.fwb` to inspect an independently
obtained image. Missing files, truncated input and directories must fail.
No check downloads or redistributes firmware.

For cross-builds, compile each test with its actual `sys/dev/ic/cixvpu_*.c`
implementation. Select the common GCC16 cross compiler and an explicit EmberBSD
sysroot. For example, set `TARGET_CC` and `TARGET_SYSROOT`, then build:

```sh
"$TARGET_CC" --sysroot="$TARGET_SYSROOT" -std=c11 -O2 -g \
  -Wall -Wextra -Werror -Isys/dev/ic \
  sys/dev/ic/cixvpu_ring.c ember/tools/cixvpu-ring-contract.c \
  -o cixvpu-ring-contract
```

Execute target programs on EmberBSD; do not install build tools on the target.
The [source CI](../../.github/workflows/cix-vpu.yml) uses a pinned official GCC16
image. Its host contracts and memory analysis do not establish kernel attachment.

## Recorded verification and limits

GCC 16.2.0 source contracts pass on Linux and EmberBSD/AArch64:
442 firmware assertions, 229165 ring assertions and 12394 page-entry assertions.
Tests exercise real shared source, malformed data, wraparound and transactional
failure behavior. Mutations of plain and checksum ring rules are detected.
The actual encoder image's layout also passes in the AArch64 guest.

The three core units compile in freestanding `_KERNEL` mode against actual
EmberBSD headers. GCC path analysis, Clang target analysis and Cppcheck were run.
Cppcheck's target run excludes only three unused inline standard-header APIs
at exact file/line locations; the initial findings are retained.
Linux Memcheck runs with origins, all leak kinds and a failure exit status.
The core tests and firmware CLI have zero reported memory errors or leaks.
These checks are not a full kernel build, kernel memory analysis or DMA test.

[Ports diagnostics](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media/cix-vpu)
cross-build FFmpeg 9.0.2 M2M modules and the actual GStreamer 1.28.8 V4L2 plugin.
All 58 selected AArch64 ABI values match between native and bundled headers.
In a compiler-free EmberBSD VM with no video device, FFmpeg registers HEVC M2M
and refuses eight real initialization attempts. Four semaphore lifetime cases
also pass. A real missing-device SIGSEGV found during this work was fixed.
The actual GStreamer plugin loads and creates no HEVC hardware factory.
The VM uses the accepted base/kernel revision
`6dd57144183678938164b4c2dcfc370cdbde8473`, which contains no CIX attachment.
No software fallback, simulated device or firmware execution establishes hardware support.

Ports records inherited analyzer findings and GStreamer's retained GLib globals.
The complete revised FFmpeg package and its installed consumers are not yet requalified.

## Remaining driver work

The reference ACPI identifier is `CIXH3010`. Read the target's actual ACPI
resources; do not assume a Linux DTS register range or interrupt number.
Establish clock/reset and power ownership before touching the block.
Current SCMI support has no CIX clock/reset provider.
The VPU has its own MMU. Use bus_dma for any platform translation; do not
assume IOMMU bypass or infer a mandatory external SMMU from another device.

Implement ACPI attachment, resource lifetime, power/clocks/reset, firmware page
loading, MMIO, interrupt handling, bounded waits and DMA synchronization.
Add native M2M input/output queues, controls, buffers, poll and stream lifetime.
Reset and close during a frame must stop DMA before releasing resources.
Late completion must not reach a closed or reused session.
Then connect the real device to FFmpeg/GStreamer and qualify the complete packages.
No physical CIX endpoint is available in the current cloud environment.

## Physical acceptance and recovery

Record board revision, firmware, source, selected kernel/modules and all hashes.
Encode two concurrent 2048x1500, 8-bit streams at 9 fps, at most 4 Mbit/s each,
for 30 minutes under parallel CPU load. Record frame counts, timestamps,
per-frame latency and independently decoded output.
Exercise absent firmware, block reset and close in the middle of a frame.
H.264 and decoding require separate acceptance records.

Use a separately selected test kernel and preserve the accepted boot choice.
Keep original boot artifacts and a working UART recovery path.
Do not overwrite firmware or persistent application data.
A repeatable boot, working memory and a result transport are prerequisites.

## References

- [Maintained Sky1 kernel patches](https://github.com/Sky1-Linux/linux-sky1/tree/57e018a398248d7e5e4d798610df79a557c0629f/patches).
- [CIX codec reference](https://github.com/Sky1-Linux/linux-sky1/blob/57e018a398248d7e5e4d798610df79a557c0629f/patches/0010-media-cix-Add-Sky1-video-codec-VPU-driver.patch).
- [Firmware provenance](https://github.com/Sky1-Linux/sky1-firmware/tree/dd81690747ddb092bcc2a221daa0f8f679ee4bc4).
