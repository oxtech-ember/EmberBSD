# Native Zynq RX and USB implementation plan

> For agentic workers: use the approved parallel execution method. Each
> implementer owns a separate main clone and a disjoint file set. Apply TDD,
> preserve existing consumers, and obtain independent review before publication.

**Goal:** Deliver native continuous RX to host SDR applications on EmberBSD.

**Architecture:** Separate the ADI acquisition queue, Zynq peripheral USB
controller, and userland IIO service. Start with explicit owned copies between
layers. Test deterministic USB data before connecting the radio stream.

**Tech stack:** EmberBSD ARMv7, GCC16, bus_space/bus_dma, POSIX C, libiio 0.25
USB wire compatibility, existing external ADS-B and AIS decoders.

**Spec:** [Native USB RX design](zynq-usb-rx-design.md), approved by the owner.

## Global constraints

- The board runs EmberBSD. No Linux driver, kernel, sysfs, or FunctionFS backend.
- Receive only; experimental tuning is 70 MHz to 6 GHz, bandwidth at most 20 MHz.
- Candidates boot in RAM. Do not write QSPI or persistent boot configuration.
- Preserve finite ABI version 1 and its existing scanner.
- Initial payload requirements: ADS-B 2.4 MSPS, AIS 3.072 MSPS, four bytes per IQ.
- Never hide overflow, discontinuity, unsupported FIR, or actual clock rounding.
- Failed DMA drain retains resources and blocks restart. No unproved unmapping.
- Preserve target ABI, actual-source tests, licenses, and upstream revision pins.
- Local experimental GCC16 tool selection uses the owner's existing exception;
  do not describe those builds as qualified SDK releases.

## Review focus

- Disconnect or stop racing with either DMA engine must not release live memory.
- Partial reads and consumer stalls must not reuse unread data or hide overflow.
- Delayed IRQs after retuning must not publish an earlier stream generation.
- Descriptor boundary, short packet, and malformed EP0 requests must stay bounded.
- Low sample-rate clock quantization must not become silent incorrect metadata.

## Task 1: Native finite IQ diagnostic

**Owner:** Examples. This consumer does not change the kernel finite ABI.

**Files:**
- Add `robotics/pluto-scanner/src/pluto_iq.c` and a focused adapter header if needed.
- Add `robotics/pluto-scanner/tests/iq_test.c` and integrate its runner/Makefile.
- Update `robotics/pluto-scanner/README.md` for the exact finite format.

**Interface:** `pluto-iq DEVICE FREQUENCY RATE BANDWIDTH SAMPLE_COUNT` writes a
versioned ASCII header, indexed hexadecimal IQ payload, and final status.
The header carries requested settings, IQ layout, count, and discontinuity
boundary. The first read requests the complete finite buffer. STATUS and TX
snapshot remain separate hardware evidence. Do not claim an actual clock
measurement that ABI1 cannot provide.

- [ ] Write failing actual-adapter tests for CONFIG/CAPTURE/read/STATUS order,
  maximum 262144 samples, eight-byte alignment, invalid input, short read,
  interrupted read, failed capture, and stale result refusal.
- [ ] Run the tests and retain the expected RED output before implementation.
- [ ] Implement bounded allocation, full first read, unambiguous framing, and
  failure exit status. Never emit a successful footer after a failed capture.
- [ ] Run host tests with GCC16 and ASan/UBSan, Cppcheck, and path analysis.
  Cross-build against the existing coherent ARMv7 application sysroot.
- [ ] Review and publish; include the binary in a RAM candidate. Capture
  separate blocks at 1090 MHz/2.4 MSPS and 162 MHz/3.072 MSPS when accepted.
- [ ] Decode each block separately on the host. Save hashes and decoder output;
  an empty result is not a hardware failure or a positive reception claim.

## Task 2: Zynq USB peripheral controller

**Owner:** OS; independent of the ADC queue implementation.

**Files:**
- Add `sys/arch/arm/xilinx/zynq_usb_device.c` and its register/descriptor header.
- Add a small USB function interface under `sys/dev/usb/` only where required.
- Modify `sys/arch/arm/xilinx/files.zynq` and FDT role matching.
- Add actual-source tests under `ember/tools/tests/zynq-usb-device/`.

**Interface:** A controller-owned endpoint request has kernel buffer, length,
direction, completion status, and generation. The function layer configures
endpoints, submits requests, cancels an endpoint, and receives setup/reset
events. No user physical addresses or unchecked device pointers enter DMA.
Freeze exported C signatures with the consumer before adding a second owner.

- [ ] Pin and audit the MIT AMD USBPS donor named in the spec. Map its device
  sequences to native bus_space/bus_dma; do not import its board support stack.
- [ ] Write failing tests for role selection, 2048-byte queue-head alignment,
  32-byte descriptor alignment, five-page descriptor splitting, and bounded
  reset/flush timeout. Test actual controller source through kernel API shims.
- [ ] Implement separate peripheral attachment, clock/PHY setup and safe cold
  initialization. Preserve host EHCI operation and avoid host VBUS drive.
- [ ] Add EP0 tests for GET_DESCRIPTOR, SET_ADDRESS after status completion,
  GET/SET_CONFIGURATION, GET_STATUS, endpoint halt, malformed lengths, new setup
  during an old transfer, bus reset, suspend, and disconnect.
- [ ] Implement EP0 and endpoint ownership, setup/add-descriptor tripwires,
  IRQ completion and checked cancellation. Do not enable streams before config.
- [ ] Add deterministic counter/loopback bulk function. Test stale IRQs, partial
  completion, short packets, timeout, repeated start/stop and retained DMA.
- [ ] Run actual-source sanitizers/analyzers and ARMv7 object/kernel builds.
  Independently review and publish the coherent source change.
- [ ] Boot a RAM candidate and qualify USB high speed and at least 12.288 MB/s
  ordered payload on Mac: short run, then 30 minutes, reset and cable removal.

## Task 3: Queued ADI acquisition

**Owner:** OS; preserve the finite API and all other DMAC consumers.

**Files:**
- Modify `sys/dev/fdt/adi_axi_dmac.c` and `adi_axi_dmacvar.h`.
- Modify `sys/dev/fdt/adi_axi_adc.c` and add an additive stream ABI header.
- Extend `ember/tools/tests/adi-axi-dmac/` and `adi-axi-adc-cdev/`.

**Interface:** Kernel stream operations start a bounded queue, return completed
blocks, release consumed blocks, expose sequence/generation and loss counters,
and stop with a checked drain. Userspace gets blocking/nonblocking read and
poll readiness. The finite CONFIG/CAPTURE/STATUS ABI remains unchanged.
Define the additive stream structs and ioctl numbers in the implementation
review; do not reuse reserved ABI1 fields without an explicit version contract.

- [ ] Write failing tests for multiple queued submissions, ID wrap, unrelated
  interrupts, delayed completions, full queue, and consumer backpressure.
- [ ] Implement the documented hardware queue with explicit buffer states.
  Avoid per-block disable/drain. Refuse unsupported hardware capabilities.
- [ ] Write failing tests for partial reads, EAGAIN/poll, stop while blocked,
  close/cancel, retune generation, overflow, and timeout quarantine.
- [ ] Implement the ADC stream consumer and retain single-session ownership.
  Publish only synchronized completed bytes and expose every known loss.
- [ ] Test requested versus actual 2.4/3.072 MSPS clock paths using real no-OS
  source. Define a justified quantization bound before changing exact checks.
  Add actual rate reporting if the stream consumer needs new metadata.
- [ ] Run existing finite, concurrent, shutdown, RX/TX ownership regressions,
  sanitizers, Cppcheck and path analysis. Cross-build and review independently.
- [ ] Publish, boot in RAM, and verify sustained capture, counters and stop.

## Task 4: Receive-only IIO USB service

**Owner:** A userland application consumes the OS APIs from tasks 2 and 3.

**Files:** Place the service beside the standalone receiver in Examples.
Package shared external dependencies through Ports; do not duplicate recipes.

**Interface:** USB interface string `IIO`; EP0 vendor interface requests 0/1/2
for reset/open/close pipe. Endpoint pairs `0x81/0x01` and `0x82/0x02` serve
control and data. Implement the required libiio 0.25 XML, attribute and buffer
commands for `ad9361-phy` and `cf-ad9361-lpc`. Reject TX and unsupported commands.

- [ ] Audit exact host calls from SDR++ and LocalRadar, including libad9361
  rate setup. Freeze the supported attribute matrix and error responses.
- [ ] Write failing parser/transport tests for partial commands, oversized
  input, invalid channel masks, unsupported attributes and disconnect cleanup.
- [ ] Implement RX command mapping and stream reads with owned copies. Do not
  acknowledge unapplied FIR or claim loss-free acquisition after overflow.
- [ ] Verify with real pinned host libiio, then SDR++ and both existing decoders.
  Review, publish, and cross-build a coherent RAM root and kernel candidate.

## Task 5: Physical acceptance and operator integration boundary

- [ ] Accept SDR++ audio and an hour of measured native streaming; record
  actual duration, rates, delivered bytes, losses and failure recovery.
- [ ] Accept uncorrected CRC-valid ADS-B messages from Nano/EmberBSD.
- [ ] Accept RF-validated AIS messages on A/B with type/MMSI and valid NMEA.
- [ ] Preserve a separate record for old RTL fixtures and live Nano evidence.
- [ ] Proceed with the operator application's separately reviewed AIS contract:
  authoritative server state, optional API data, existing Qt presentation,
  freshness/error handling, and no automatic optical association.
- [ ] Update DevBoards only for scenarios physically accepted. Preserve the
  accepted RAM images, recovery image, source pins and essential receipts;
  clean only verified temporary build resources after saving unique work.
