# VideoCore mailbox transactions

The native mailbox transport supports the existing Raspberry Pi firmware
property callers. This change bounds `bcmmbox_request()` and preserves DMA
ownership when firmware completion is unknown. It does not add a V3D driver,
change firmware, or establish GPU support on Compute Module 5.

## Ownership and errors

Each channel has its own transaction mutex. A request owns its channel from
preflight through response validation, copying and DMA cleanup. Public
`bcmmbox_read()` and `bcmmbox_write()` use the same channel mutex. The shared
transmit mutex covers one FIFO check and write, and is released before any
wait. A property request waiting on channel 8 therefore does not exclude
an unrelated VCHIQ write on channel 3.

Transmit availability comes from **mailbox 1 status, offset `0x38`**.
Mailbox 0 status describes the receive FIFO. The response must carry the
same channel and exact DMA address as the submitted request. Addresses
must be 16-byte aligned and the complete mapped buffer must fit in the
32-bit mailbox address range. All existing `bcmmbox_request()` callers use
property channel 8; its firmware protocol requires the address echo.

The hardware transaction has one monotonic one-second deadline, established
before transmission and shared by TX, polling RX and interrupt-driven RX.
Wakeups do not renew it. Polling releases the interrupt mutex before each
10-microsecond delay. Each FIFO drain handles at most 16 entries; a further
100,000-wait limit prevents a stalled clock or continuous wakeups from
making the loop infinite. Allocation and scheduling are not a real-time
latency guarantee. No interrupt handler performs a sleeping request.

| Failure | Result and ownership |
| --- | --- |
| Invalid arguments or absent attached provider | `EINVAL` or `ENXIO`, without allocation or submission |
| DMA allocation/mapping failure | Original allocator error; release all resources acquired so far |
| DMA address cannot be represented | `EFBIG`; release the unsubmitted map and buffer |
| TX deadline before submission | `ETIMEDOUT`; synchronize and release the unsubmitted DMA resources; a later request may retry |
| RX deadline after submission | `ETIMEDOUT`; retain the map and buffer and quarantine this channel |
| Mismatched address or receive-slot overflow | `EIO`; after submission, retain the map and buffer and quarantine this channel |
| Unclaimed response already pending before a request | `EIO`; quarantine before any allocation or submission |
| Request or read on a quarantined channel | `EIO` before allocation or consumption; a legacy void write diagnoses and performs no write |

Request output bytes and `pres` remain unchanged on error. A quarantined
channel stays closed until reboot. Late responses are discarded without
reopening it or releasing retained DMA. Repeated requests cannot allocate
more buffers on that channel. Other channels remain available. Firmware
may have performed a requested operation before a timeout; this transport
does not undo firmware side effects.

## Preserved legacy boundaries

The early `bcm2835_mbox_read()` and `bcm2835_mbox_write()` interfaces remain
void and unbounded. They run from `bcm283x_platform` before mailbox attach,
with static buffers. Their write now uses the correct TX status register.
Adding the attached driver's delay or sleep rules there would be unsafe:
the original BCM2835 delay provider needs its timer driver to be attached.

The public void `bcmmbox_write()` used by VCHIQ keeps its waiting semantics,
but releases the shared TX mutex between attempts. This is **not** bounded
VCHIQ initialization. VCHIQ starts threads before publishing its slot
buffer; introducing a new timeout cleanup there requires a separate
thread and DMA lifetime review. The attached public read/write entry points
share the channel mutex; the raw helpers run only in platform bootstrap.

## Validation and provenance

Run the host or native software contract from the source root:

```sh
sh ember/tools/bcmmbox-contract.sh "$PWD"
CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/bcmmbox-contract.sh "$PWD"
```

The runner extracts the actual mailbox C functions and header. Its fake
bus and DMA provider distinguishes RX and TX state, tracks publication and
resource lifetime, and supplies a monotonic clock. Real threads exercise
two requests and the public split APIs, including channel 3 progress while
channel 8 waits. Controls cover cold polling, ACPI polling, FDT interrupts,
spurious wakeups, late/mismatched responses, overflow, deadline exhaustion,
quarantine, pre-TX cleanup, invalid DMA addresses and legacy success paths.
Compiled causal mutants must fail behavioral assertions.

On 2026-10-08, 70 actual-source cases passed on macOS with assertions both
enabled and disabled, and under AddressSanitizer/UndefinedBehaviorSanitizer.
Eight compiled mutants were rejected. The original baseline failed three
behavioral controls for TX status, response ownership and concurrency. GCC
16.2 cross-compiled the mailbox core, raw helpers and both ACPI/FDT attachment
objects with `-Werror`. The same 70 fake-I/O cases also passed as an AArch64
executable in an isolated EmberBSD/NetBSD 11 VM. This is target software
contract acceptance, not physical firmware, interrupt or DMA validation.

## Compute Module 5 hardware acceptance

On 2026-10-08, the complete GCC16.2 `EMBER64` kernel from
[`eec8a2229dd592ec0d9cd7dd58f1407dcddc4f19`](https://github.com/oxtech-ember/EmberBSD/commit/eec8a2229dd592ec0d9cd7dd58f1407dcddc4f19)
booted on a Raspberry Pi Compute Module 5, board revision `C04180`.
EDK2 reported version `rpi5-20260730.1`, date `20260928`.
The kernel ELF SHA256 was
`8d67ee425c4fb52df276df266864aa6a9bfff634f2ab5701133cf7465d9d06e4`.
Its four board module files matched the same build; `if_cemac_acpi` and
`rpi5button` loaded normally. SSH returned after the normal reboot.

Run the read-only property-consumer check on the board as root, using a
new output directory:

```sh
sh ember/tools/bcmmbox-hardware-read.sh /var/tmp/mailbox-read-check
```

The [runner](../tools/bcmmbox-hardware-read.sh) makes 128 clock requests in
32 rounds of four processes, reads temperature/throttle sensors each round,
and makes a final clock request. It checks all exit statuses, numeric bounds,
valid temperature, clear voltage/throttle flags, and unchanged kernel messages.
Each `machdep.cpu.frequency.current` read invokes a firmware GET_CLOCKRATE;
it is not a cached frequency result. The script creates only its output
directory and logs. It changes no frequency, power, network or boot setting.
A failure retains the partial logs. Run it on an otherwise quiet board;
unrelated new kernel diagnostics also prevent a PASS.

The hardware run passed all requests and sensor checks in 34 seconds.
This validates successful firmware transactions, including real DMA buffers
and address echoes. **ACPI uses polling:** interrupt establishment remains
disabled in that attachment even though its ACPI resource includes an IRQ.
No malformed response, hardware timeout, quarantine, VCHIQ operation or
long-duration stress was injected. These results do not establish IRQ
handling or V3D acceleration. The preceding AArch64 VM also booted this
exact kernel, ran both A733 software fixtures, and shut down normally.

## Source references

The reviewed baseline is EmberBSD
[`dd6b5815f5f82446a5d652dd2dd491e078aeb140`](https://github.com/oxtech-ember/EmberBSD/commit/dd6b5815f5f82446a5d652dd2dd491e078aeb140).
The earlier [polling timeout adaptation](../patches/bcmmbox-timeout.diff)
is already applied. It retained DMA after a polling timeout, but did not
serialize a complete request or bound TX and interrupt waits.

The register distinction is confirmed by Raspberry Pi Linux
[`bcm2835-mailbox.c` at `43c132e8863c3bff3647033b6a7d2bf87b15501c`](https://github.com/raspberrypi/linux/blob/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/mailbox/bcm2835-mailbox.c):
`MAIL1_STA` is `0x38`, and `bcm2835_last_tx_done()` reads it. The
[official property protocol](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)
requires the response to preserve the buffer address. These are protocol
references; no Linux implementation is imported. NetBSD licenses and
upstream identifiers remain intact. Source contracts and cross-compilation
alone do not establish physical mailbox or GPU acceptance; the bounded
CM5 successful-request scenario above is a separate hardware result.
