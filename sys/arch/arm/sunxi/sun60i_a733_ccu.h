/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SUN60I_A733_CCU_H
#define _SUN60I_A733_CCU_H

struct clk;

enum sun60i_a733_gpu_reg {
	A733_GPU_REF, A733_GPU_PERIPH, A733_GPU_PERIPH_PAT0,
	A733_GPU_PERIPH_PAT1, A733_GPU_PLL, A733_GPU_PAT0, A733_GPU_PAT1,
	A733_GPU_MODULE, A733_GPU_BUS, A733_GPU_AHB, A733_GPU_MASTER,
	A733_GPU_PERIPH_GATE_EN, A733_GPU_PERIPH_GATE_STAT,
	A733_GPU_NREGS
};

enum sun60i_a733_gpu_reason {
	A733_GPU_READY, A733_GPU_SNAPSHOT_CHANGED, A733_GPU_HOSC_CHANGED,
	A733_GPU_MODULE_GATED, A733_GPU_UPDATE_PENDING, A733_GPU_BUS_GATED,
	A733_GPU_RESET_ASSERTED, A733_GPU_MASTER_GATED, A733_GPU_REF_FLAGS,
	A733_GPU_REF_RATE, A733_GPU_CORE_PARENT, A733_GPU_CORE_PLL,
	A733_GPU_CORE_DIVIDER, A733_GPU_AHB_PARENT, A733_GPU_AHB_PLL,
	A733_GPU_AHB_DIVIDER, A733_GPU_DCXO_CHANGED
};

/*
 * Inspection success means a complete observation, not GPU readiness.
 * A nonzero API return leaves the caller's state unchanged.
 */
struct sun60i_a733_gpu_state {
	uint32_t sample[2][A733_GPU_NREGS];
	uint32_t changed;	/* One bit per register whose samples differ. */
	/*
	 * Diagnostic only: bits 0..11 correspond to PERI0 gates 16..27.
	 * 400M=1, 400M_ALL=2, 600M=9, 800M=10; one means enabled,
	 * except no_auto where one means automatic gating is disabled.
	 * These observations do not change readiness or reserve the gates.
	 */
	struct {
		uint32_t configured, no_auto, effective;
	} periph_gates[2];
	u_int hosc_hz[2];
	uint32_t dcxo_sample[2][2];	/* Queries bracketing the CCU reads. */
	u_int dcxo_hz[2];
	enum sun60i_a733_gpu_reason reason;
	int readiness_error;
	u_int core_hz, bus_hz;	/* Valid only when readiness_error is zero. */
};

int	sun60i_a733_ccu_gpu_inspect(struct clk *,
	    struct sun60i_a733_gpu_state *);

/* Experimental boot ownership; release is forbidden after any attempted write. */
int	sun60i_a733_ccu_gpu_reserve(struct clk *, const void *);
/* Local writes, then bounded UPDATE completion; timeout retains ownership. */
int	sun60i_a733_ccu_gpu_prepare(struct clk *, const void *, bool *);
/* Opt-in PLL600 cold start; caller must first establish its operating voltage. */
int	sun60i_a733_ccu_gpu_prepare_pll(struct clk *, const void *, bool *);
/* Fixed NPU local profile. PCK4 must be retained before prepare. */
int	sun60i_a733_ccu_npu_reserve(struct clk *, const void *);
int	sun60i_a733_ccu_npu_prepare(struct clk *, const void *, bool *);
int	sun60i_a733_ccu_npu_release(struct clk *, const void *);
/* Fresh proof of this owner's completed retained profile. No NPU MMIO. */
int	sun60i_a733_ccu_npu_ready(struct clk *, const void *);
int	sun60i_a733_ccu_gpu_release(struct clk *, const void *);

/* One native DISPLAY coordinator borrows PCK1/10 and unchanged parents. */
struct sun60i_a733_display_owner_status;
int	sun60i_a733_ccu_display_reserve(struct clk *, int, const void *);
int	sun60i_a733_ccu_display_release(struct clk *, const void *);
/* PCK retention precedes local DE0 writes. Errors can still retain both. */
int	sun60i_a733_ccu_display_begin_de0(struct clk *, const void *, bool *);
/* The same retained DE0 owner enables HDMI SFR at exact SYS_CLK24M. */
int	sun60i_a733_ccu_display_begin_hdmi(struct clk *, const void *);
/* Call after begin_hdmi and native PHY preparation. No pixel rate is set here. */
int	sun60i_a733_ccu_display_begin_tv0(struct clk *, const void *);
/* Fresh admitted SFR clock fields. Failure leaves the output unchanged. */
struct sun60i_a733_display_hdmi_clock {
	uint32_t source;
	u_int divisor, hz;
};
int	sun60i_a733_ccu_display_hdmi_clock_state(struct clk *, const void *,
	    struct sun60i_a733_display_hdmi_clock *);
/* Fresh local CCU/PCK proof only. This does not authorize DMA or output MMIO. */
int	sun60i_a733_ccu_display_ready(struct clk *, const void *);
int	sun60i_a733_ccu_display_status(struct clk *, const void *,
	    struct sun60i_a733_display_owner_status *);
/* Sticky accounting only. This function performs no hardware write. */
int	sun60i_a733_ccu_display_fault(struct clk *, const void *, int);

/* Fixed raw memory-path order. A snapshot does not establish DMA readiness. */
enum sun60i_a733_display_memory_reg {
	A733_DISPLAY_MEMORY_IOMMU1, A733_DISPLAY_MEMORY_NSI_CFG,
	A733_DISPLAY_MEMORY_MBUS, A733_DISPLAY_MEMORY_MBUS_MASTER,
	A733_DISPLAY_MEMORY_PERIPH_GATE, A733_DISPLAY_MEMORY_PERIPH_STATUS,
	A733_DISPLAY_MEMORY_NSI, A733_DISPLAY_NMEMORY_REGS
};
/* Claim before begin_de0. Clean CCU release also releases this subclaim. */
int	sun60i_a733_ccu_display_memory_reserve(struct clk *, const void *);
/* Complete fresh raw samples under the matching claim and PCK lease. */
int	sun60i_a733_ccu_display_memory_state(struct clk *, const void *,
	    uint32_t [2][A733_DISPLAY_NMEMORY_REGS]);

/* Read-only readiness observation, not a reservation against future writers. */
int	sun60i_a733_ccu_gpu_ready(struct clk *, u_int *, u_int *);

/*
 * Clock and reset numbers follow the device tree binding
 * allwinner,sun60i-a733-ccu as posted for Linux (v3, September 2026).
 * Only the clocks this driver implements are listed.
 * Origin: EmberBSD; AI-assisted accelerator providers, see
 * ember/boot/a733-accelerator-clocks.md for the pinned hardware sources.
 */

#define	A733_CLK_PLL_REF		0
#define	A733_CLK_SYS_24M		1
#define	A733_CLK_PLL_PERIPH0_4X		3
#define	A733_CLK_PLL_PERIPH0_2X		4
#define	A733_CLK_PLL_PERIPH0_800M	5
#define	A733_CLK_PLL_PERIPH0_480M	6
#define	A733_CLK_PLL_PERIPH0_600M	7
#define	A733_CLK_PLL_PERIPH0_400M	8
#define	A733_CLK_PLL_PERIPH0_300M	9
#define	A733_CLK_PLL_PERIPH0_200M	10
#define	A733_CLK_PLL_PERIPH0_160M	11
#define	A733_CLK_PLL_PERIPH0_150M	12
#define	A733_CLK_PLL_GPU0		23
#define	A733_CLK_PLL_NPU		39
#define	A733_CLK_AHB			43
#define	A733_CLK_APB0			44
#define	A733_CLK_APB1			45
#define	A733_CLK_APB_UART		46
#define	A733_CLK_AHB_NPU		65
#define	A733_CLK_AHB_GPU0		66
#define	A733_CLK_MBUS_GPU0		77
#define	A733_CLK_MBUS_NPU		78
#define	A733_CLK_MBUS_CE		86
#define	A733_CLK_MBUS_GMAC0		91
#define	A733_CLK_BUS_PWM0		98
#define	A733_CLK_DE0			114
#define	A733_CLK_CE			127
#define	A733_CLK_BUS_CE		128
#define	A733_CLK_BUS_CE_SYS		129
#define	A733_CLK_NPU			130
#define	A733_CLK_BUS_NPU		131
#define	A733_CLK_GPU0			132
#define	A733_CLK_BUS_GPU0		133
#define	A733_CLK_MMC0			139
#define	A733_CLK_BUS_MMC0		140
#define	A733_CLK_MMC1			141
#define	A733_CLK_BUS_MMC1		142
#define	A733_CLK_MMC2			143
#define	A733_CLK_BUS_MMC2		144
#define	A733_CLK_BUS_UART0		150
#define	A733_CLK_BUS_UART1		151
#define	A733_CLK_BUS_UART2		152
#define	A733_CLK_BUS_UART3		153
#define	A733_CLK_BUS_UART4		154
#define	A733_CLK_BUS_UART5		155
#define	A733_CLK_BUS_UART6		156
#define	A733_CLK_BUS_I2C0		157
#define	A733_CLK_BUS_I2C1		158
#define	A733_CLK_BUS_I2C2		159
#define	A733_CLK_BUS_I2C3		160
#define	A733_CLK_GPADC0_24M		182
#define	A733_CLK_BUS_THS0		184
#define	A733_CLK_USB_OHCI0		210
#define	A733_CLK_BUS_OHCI0		211
#define	A733_CLK_BUS_EHCI0		212
#define	A733_CLK_BUS_OTG		213
#define	A733_CLK_USB_OHCI1		214
#define	A733_CLK_BUS_OHCI1		215
#define	A733_CLK_BUS_EHCI1		216
#define	A733_CLK_USB2_U2_REF		218
#define	A733_CLK_USB2_SUSPEND		219
#define	A733_CLK_USB2_MF		220
#define	A733_CLK_USB2_U3_UTMI		221
#define	A733_CLK_USB2_U2_PIPE		222
#define	A733_CLK_SERDES_PHY		225
#define	A733_CLK_GMAC0_PHY		227
#define	A733_CLK_BUS_GMAC0		228

#define	A733_RST_BUS_PWM0		13
#define	A733_RST_BUS_CE		25
#define	A733_RST_BUS_CE_SYS		26
#define	A733_RST_BUS_NPU_CORE		27
#define	A733_RST_BUS_NPU_AXI		28
#define	A733_RST_BUS_NPU_AHB		29
#define	A733_RST_BUS_NPU_SRAM		30
#define	A733_RST_BUS_GPU0		31
#define	A733_RST_BUS_MMC0		34
#define	A733_RST_BUS_MMC1		35
#define	A733_RST_BUS_MMC2		36
#define	A733_RST_BUS_UART0		42
#define	A733_RST_BUS_UART1		43
#define	A733_RST_BUS_UART2		44
#define	A733_RST_BUS_UART3		45
#define	A733_RST_BUS_UART4		46
#define	A733_RST_BUS_UART5		47
#define	A733_RST_BUS_UART6		48
#define	A733_RST_BUS_I2C0		49
#define	A733_RST_BUS_I2C1		50
#define	A733_RST_BUS_I2C2		51
#define	A733_RST_BUS_I2C3		52
#define	A733_RST_BUS_THS0		69
#define	A733_RST_USB_PHY0		82
#define	A733_RST_BUS_OHCI0		83
#define	A733_RST_BUS_EHCI0		84
#define	A733_RST_BUS_OTG		85
#define	A733_RST_USB_PHY1		86
#define	A733_RST_BUS_OHCI1		87
#define	A733_RST_BUS_EHCI1		88
#define	A733_RST_BUS_USB2		89
#define	A733_RST_BUS_SERDES		92
#define	A733_RST_BUS_GMAC0		93
#define	A733_RST_BUS_GMAC0_AXI		94

/*
 * Reserve inactive IOMMU1 peers before DISPLAY retention. Requires the
 * memory claim. Zero gate/reset fields and a static PCK2 snapshot are kept
 * under the same native ownership lock. No global IOMMU operation is granted.
 * State publishes two complete raw samples only on success.
 */
enum { A733_DISPLAY_NISOLATION_REGS = 10 };
int sun60i_a733_ccu_display_isolation_reserve(struct clk *, const void *);
int sun60i_a733_ccu_display_isolation_state(struct clk *, const void *,
    uint32_t [2][A733_DISPLAY_NISOLATION_REGS]);

#endif /* _SUN60I_A733_CCU_H */
