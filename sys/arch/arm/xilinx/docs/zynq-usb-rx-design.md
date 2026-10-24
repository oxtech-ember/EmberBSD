# Native Zynq USB receive streaming: proposed design

Status: design for review. This document does not claim a working USB device
driver, continuous acquisition, IIO compatibility, or decoded RF messages.

## Goal and existing support

Run the radio on EmberBSD. Provide continuous receive data to existing host
applications without a Linux kernel, Linux userland, or Linux driver backend
on the board. Keep frequency selection from 70 MHz to 6 GHz experimental.
Do not add RF transmission or persistent installation to this stage.

The existing AD9363A provider preserves chip identity and limits RF bandwidth
to 20 MHz. An explicit receive-only FDT property selects extended tuning.
The ADC character device and ADI DMAC provider currently support finite
captures. Their positive drain and failed-drain retention rules remain valid.
The Zynq USB attachment currently selects EHCI host mode. Hardware device
capability and role constants do not implement peripheral mode.

The primary host requirements are:

| Consumer | Center | Required complex sample rate | Native sample bytes |
| --- | --- | --- | --- |
| ADS-B decoder | 1090 MHz | 2,400,000 per second | 4 |
| Two-channel AIS decoder | 162 MHz | 1,536,000 per second | 4 |
| AIS decoder without RX FIR | 162 MHz | 3,072,000 per second | 4 |
| SDR application | 70 MHz to 6 GHz | Supported selectable rates | 4 |

One RX local oscillator cannot receive ADS-B and AIS simultaneously. Test
these modes separately. A 2.4 MSPS stream requires 9,600,000 payload bytes
per second before USB protocol overhead. AIS at 3.072 MSPS requires
12,288,000 payload bytes per second. RF bandwidth, sample rate, and
delivered USB throughput are separate quantities.

## Selected boundaries

Use a native USB peripheral driver with bulk endpoints and an RX subset of
the libiio 0.25 USB protocol. Keep USB controller mechanics in the kernel.
Keep IIO names, XML, attribute policy, and command parsing in userland.
Applications continue to use the existing host libiio backend.

The first implementation copies completed ADC data into userland and then
into separately owned USB buffers. Do not share DMA mappings between the
ADC and USB controllers. Zero-copy needs a separate ownership contract.

CDC networking plus a network IIO service is an alternative. It adds an
Ethernet function and network setup before it solves the same IIO mapping.
A private USB wire protocol is another alternative. It needs host adapters
for every current consumer. Neither is the first implementation target.

Do not use Linux FunctionFS or Linux IIO sysfs as an EmberBSD dependency.
The vendor USBPS controller sources can inform controller sequencing. Verify
the license of each adapted file and record its exact revision. Preserve
the existing BSD USB host path and its configuration.

## Controller milestone

Add a separate FDT peripheral attachment for the Zynq controller. Validate
the requested role, PHY, clocks, interrupts, address range, DMA alignment,
and available endpoints before attaching. Do not drive host VBUS in device
mode or attach host and peripheral drivers to the same controller.

Implement EP0 enumeration, address and configuration transitions, endpoint
halt, bus reset, disconnect, suspend, and bounded endpoint teardown. Model
queue heads and transfer descriptors with explicit DMA ownership and cache
synchronization. Reject invalid setup lengths and unsupported requests.
Use completion handling appropriate for the controller's descriptor rules.
An interrupt alone does not return ownership of every descriptor.

Before RF integration, send a deterministic counter through bulk endpoints.
On a high-speed Mac connection, verify at least 12.288 MB/s of payload, exact
ordering, and clean cancellation. A short test precedes a 30-minute run.
Reset and disconnect tests must retain mappings if drain cannot be proved.

## Acquisition milestone

Add queued RX submissions to the ADI DMAC provider and a bounded stream API
to the ADC character device. Preserve the existing finite ABI and scanner.
Do not implement streaming as repeated stop, drain, and restart calls that
silently concatenate gaps. Do not assume the FPGA enables cyclic transfers.
Use the actual core capabilities and documented queued-transfer semantics.

Track buffer states, submission IDs, completed bytes, and stream generation.
Publish only completed DMA ranges after the required memory synchronization.
Never return a buffer to DMA while a consumer still owns its readable data.
Bound queue depth and allocation size. Expose overruns, dropped blocks, and
discontinuities; never report a lossless stream after a known overflow.

Stop and close cancel submissions and wake blocked readers. A successful
drain precedes freeing DMA memory. A failed drain retains mappings and
blocks restart until the existing coordinated reset policy permits it.
Retuning first stops and drains the stream, increments its generation, and
invalidates old data. Enforce one receive session and retain TX inhibition.

Qualify sample-clock readback for each decoder rate. The current finite
10 MSPS evidence does not qualify these lower rates. The existing no-FIR
clock chain cannot produce 1.536 MSPS. First qualify AIS at 3.072 MSPS with
the existing decoder's supported input rate. Add 1.536 MSPS only with the
needed FIR configuration through the existing AD9361 provider. Define and
test clock quantization tolerance before changing the current exact-rate
check. Return the actual rate. Reject unsupported rates instead of silently
claiming the requested value. Any host resampling is explicit and tested.

## IIO compatibility milestone

Implement the interface string, vendor requests, control bulk pair, and
stream bulk pair required by the pinned libiio USB backend. Verify these
against upstream source; do not reuse an unrelated vendor's USB identity.
Use a suitable development identity with explicit host discovery selection.

Map the required `ad9361-phy` and `cf-ad9361-lpc` receive attributes and scan
elements to the native ADC ABI. Preserve signed 12-bit I/Q in little-endian
16-bit containers. Implement only documented operations and return explicit
errors for unsupported attributes, TX channels, and invalid buffer masks.
Do not silently acknowledge FIR writes that were not applied.

Bound command lengths, XML size, attribute payloads, and outstanding reads.
Disconnect cancels the session. Host errors must distinguish unsupported
configuration, timeout, overflow, and device removal.

## Physical acceptance

First use finite native IQ captures with existing file-input decoders when
the exact sample rate and enough contiguous samples are available. Preserve
capture boundaries and sample format; never hide gaps by joining files.

Then run SDR++, ADS-B, and AIS sequentially through native USB streaming.
Record source revisions, image hashes, actual rate, duration, byte counts,
queue errors, lost samples, and decoder output. Validate ADS-B with uncorrected
CRC-valid messages. Validate AIS using decoder frame checks and NMEA checksum,
channel, message type, and MMSI. A valid NMEA checksum alone does not prove
that RF frames passed decoder checks. Historical recordings or Internet
targets do not establish reception on this board.

For a one-hour run, report the actual duration and measured loss counters.
Exercise repeated stop/start, rate changes, retuning, and cable removal.
Do not infer calibrated sensitivity or frequency accuracy from PLL lock.

## Owners and exclusions

The OS owns USB peripheral and acquisition mechanisms and their tests.
An IIO service and decoder packaging belong to their application or Ports
owners. DevBoards records only the physical scenarios actually accepted.
AIS ingestion and display in a separate operator application have their own
API, state, and UI design; they are not part of this kernel change.

## Source references

- [Existing converter contracts](zynq-converters.md)
- [ADC receive ABI](../../../../dev/fdt/adi_axi_adc.h)
- [DMAC ownership API](../../../../dev/fdt/adi_axi_dmacvar.h)
- [AD9361 provenance and receive limits](../../../../external/bsd/ad9361/README)
- [libiio v0.25 USB backend](https://github.com/analogdevicesinc/libiio/blob/v0.25/usb.c)
- [AMD USBPS driver](https://github.com/Xilinx/embeddedsw/tree/f8809afec39a7048597f34f39372169be20735cf/XilinxProcessorIPLib/drivers/usbps)
