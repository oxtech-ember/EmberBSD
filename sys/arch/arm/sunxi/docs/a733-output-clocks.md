# A733 output clock phases

The native output uses the existing DISPLAY lease. The DE coordinator retains
PCK domains 1/10 before `sun60i_a733_ccu_display_begin_de0()` writes a CCU register.
Pass that same owner cookie and the same native CLK_DE0 handle to each output call.
Do not use generic clock or reset calls to bypass the lease.

Call `sun60i_a733_ccu_display_begin_hdmi()` before HDMI register access.
The fixed sequence releases MAIN reset bit16 and SUB reset bit17 at 0x168c.
It selects SYS_CLK24M source0 at 0x1690, opens SFR gate bit31, then opens HDMI bus gate bit0.
An active SFR clock with another source is refused before a write.
The shared ancestor proof requires exact 24 MHz SYS_CLK24M.
This clock is distinct from the physical DCXO input to the HDMI PHY.
Call `sun60i_a733_ccu_display_hdmi_clock_state()` to obtain fresh source, divisor
and rate fields.
Use that admitted rate to calculate DDC high and low counts.
The query leaves its output unchanged on failure.

After native PHY preparation, call `sun60i_a733_ccu_display_begin_tv0()`.
It releases TV0 reset bit16 and opens bus gate bit0 at 0x1604.
It does not select a pixel rate or start TCON output.
The native PHY owns the exact pixel-clock plan.
The output provider owns VO1 routing, TCON timing and output start.

Each phase checks the retained PCK/CCU owner and fresh protected inputs.
Each write uses a fixed mask and checked readback.
A final sample must match the expected phase and the claimed memory path.
Only then does the lease accept the new expected state.
A hardware or freshness error retains the first fault and blocks later I/O.
A wrong owner, repeated phase or invalid phase order returns EBUSY without I/O.
HDCP reset bit18, HDCP_ESM, HDMI_TV, PLL and unrelated register fields remain unchanged.

The register fields come from the A733 User Manual V1.00,
sections 4.1.6.225, 4.1.6.231 and 4.1.6.232.
The MAIN/SUB/SFR/bus order agrees with the pinned
[A733 vendor sequence](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_drm_hdmi.c).
See [the PHY source boundary](a733-hdmi-phy.md) for the separate pixel-clock core.
The implementation is independent BSD code and is AI-assisted.

Run `ember/tools/a733-accelerator-clock-contract.sh` for the source contract.
It checks numeric register sequences, every read and write failure, retained
ownership, post-write drift and prohibited HDCP masks.
These checks do not establish physical HDMI output.
