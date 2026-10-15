# CEMAC interrupt lifecycle

The Cadence driver masks device interrupts before its stopped-state handler
starts declining a shared interrupt. This matters on RP1, where Ethernet can
share the PCIe INTx line with another function.

## Stop and restart contract

`cemac_ifstop` holds the interrupt lock while it writes all bits to `ETH_IDR`.
It reads `ETH_IMR` to flush the PCIe posted write, then closes the software gate.
It releases both driver locks before it halts the tick callout or stops MII.
A dispatched tick sees the closed gate and does not rearm the callout.

Stop retains the running DMA engine, descriptor rings and buffer mappings.
It does not read or clear `ETH_ISR`. Pending receive completions must survive
until the real interrupt handler can reclaim the retained descriptors.
An ACK without ring service can leave a full receive ring without its event.

Restart keeps interrupts masked until `mii_mediachg` succeeds. Under the
interrupt lock it opens the software gate, enables the controller and unmasks
interrupts. The normal handler acknowledges and services pending causes.
A PHY setup error leaves the gate closed. A queued watchdog reset checks
`IFF_UP` under the interface lock and cannot revive an administratively down
interface. It starts queued output only after successful initialization.

This is not a complete DMA shutdown protocol. Disabling GEM transmit resets
its queue pointer. Graceful transmit teardown needs a separate ring protocol.
Stop must not restore an old fixed clock divider or data-bus width.

## Reproduce the host contract

Run from the source root:

```sh
sh ember/tests/cemac-stop.sh
CFLAGS='-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all' \
  sh ember/tests/cemac-stop.sh
```

The script extracts complete production functions and includes the real
register and state declarations. It models MMIO, locks, callouts, PHY results,
mbuf allocation and DMA ownership boundaries. It executes the real interrupt
handler with all 64 receive descriptors occupied during down/up. No new packet
is needed to drain the retained completion. Both read-clear and write-one-clear
ISR models are checked. These models do not qualify physical bus timing.

The tests cover repeated stop, shared interrupts, delayed watchdog work,
PHY failure, posted-write flushing, ring ownership and configuration retention.
Unsupported modeled operations abort instead of returning success.
`CEMAC_STOP_CASE` selects `stop`, `init`, `rx`, `reset`, `media` or `all`.
`CEMAC_STOP_SOURCE` selects a comparison driver source.
`CEMAC_STOP_BUILD` retains generated inputs for analysis.

The script also supports cross compilation:

```sh
CC="$CROSS_CC" CFLAGS="$TARGET_CFLAGS" CEMAC_STOP_COMPILE_ONLY=1 \
  CEMAC_STOP_BUILD="$OUTPUT_DIRECTORY" sh ember/tests/cemac-stop.sh
```

Run the resulting `contract` on the matching target or a supported test runner.
The test does not install a compiler on the board. On a supported host, run
`valgrind --tool=memcheck --leak-check=full --errors-for-leak-kinds=all
--error-exitcode=1 OUTPUT_DIRECTORY/contract` as one command. Darwin arm64 lacks
supported Valgrind execution; ASan/UBSan is an alternative, not a Memcheck result.

## Evidence and limits

The original stop code left interrupts enabled while the handler returned
without acknowledging them. The hardware-disable block was already excluded
before upstream commit `13f919059a55609080109e4f3d3d6855c00482e7` introduced the
stopped-state handler. That commit does not explain the earlier exclusion.
The repair restores only interrupt masking, not the excluded register reset.

Actual-source regression rejects the original stop path and mutations that
remove masking, clear retained events, open IRQs early, ignore PHY errors or
revive a down interface. ASan/UBSan passes on Darwin arm64. The complete driver
also compiles with GCC 16.2.0 and passes its `-fanalyzer` with the real EmberBSD
module headers. Cppcheck and Clang analysis are additional host checks; the
fixture is not an analysis of kernel allocation or scheduler implementations.

Hardware down/up, concurrent traffic, memory pressure and a sustained run still
need acceptance. A source defect does not establish the cause of a past board
restart. See the [CM5 hardware limits](../boards/compute-module-5.md).

The [Cadence GMAC interrupt description](https://onlinedocs.microchip.com/oxy/GUID-05624835-FA79-4FB8-AB53-EB1F2D7BCC20-en-US-10/GUID-5B6ACAC6-8168-4A4B-A562-0291A547EDA7.html)
distinguishes masking from status acknowledgement. Raspberry Pi's
[RP1 GEM configuration](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/net/ethernet/cadence/macb_main.c)
marks PCIe posted writes and uses read-clear ISR behavior. The host contract
also covers the write-one-clear variant used by other GEM integrations.
