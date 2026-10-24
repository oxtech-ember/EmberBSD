# Zynq USB APER clock contract

The native PS clock provider supports FDT clock IDs 28 and 29, USB0_APER
and USB1_APER. Both gates derive their reported rate from CPU_1x. They do
not supply or measure the separate external ULPI reference clock.

APER_CLK_CTRL at SLCR offset 0x12c uses bits 2 and 3 for USB0 and USB1.
The implementation updates only the selected bit under the shared syscon
lock. It unlocks SLCR, checks the register readback, and restores the prior
SLCR lock state. An already enabled gate causes no writes. Failed readback
returns EIO. Existing CPU, peripheral, and FPGA gate paths are unchanged.

Register references:

- AMD Zynq-7000 TRM UG585, SLCR APER_CLK_CTRL, USB0_CPU_1XCLKACT and
  USB1_CPU_1XCLKACT.
- [Upstream Linux v6.12 clock registration](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/zynq/clkc.c#L509-L516)
  confirms the bit positions and CPU_1x parent. It was used as register
  documentation only. No GPL implementation was imported.

Run with explicit native compiler and host ABI inputs:

```sh
CC=/absolute/gcc16 CFLAGS='host ABI flags' \
    python3 ember/tools/tests/zynq-usb-clock/run.py
ANALYZE=yes CPPCHECK=/absolute/cppcheck CC=/absolute/gcc16 \
    CFLAGS='host ABI flags' python3 ember/tools/tests/zynq-usb-clock/run.py
```

Add ASan/UBSan flags to CFLAGS for memory diagnostics. The runner executes
actual provider bodies and the original enum/softc with protected SLCR
boundary services. Literal IDs and masks test FDT routing, both lock states,
readback rejection, idempotency, parent rate, and preservation of other gates.
It does not establish a physical clock waveform or USB enumeration.

Cppcheck suppressions in the extracted test view cover attachment-only
fields, fixed callback signatures, inherited getter declaration scope, and
an indirect fixture failure-injection reset. They do not modify production
bodies or suppress a diagnostic category.
