# Zynq-7000 GEM link clocks

The GEM frontend requests a 2.5, 25 or 125 MHz TX reference clock for an
active 10, 100 or 1000 Mb/s link. The PS clock provider changes only the
selected GEM divisors, preserves the parent, gate and SLCR lock state, and
rejects unsupported rates or EMIO inputs. A failed clock change is reported
as link down. Other Cadence frontends retain their existing behavior.

The register definitions follow AMD Zynq-7000 TRM UG585 (SLCR GEM0_CLK_CTRL
and GEM1_CLK_CTRL), not the Zynq UltraScale+ register layout. This is an
original implementation using the existing NetBSD clock and MII interfaces.

Run the actual-source host contract from the repository root:

```sh
python3 ember/tools/tests/zynq-gem-clock/run.py
ANALYZE=yes MEMCHECK=yes python3 ember/tools/tests/zynq-gem-clock/run.py
```

The fixture checks both GEMs, 10/100/1000 transitions, parent selection,
register preservation, error paths and the common MAC callback. It models
register I/O; it does not validate PHY signaling, DMA or link renegotiation.
Physical acceptance requires traffic and speed transitions on the target.
