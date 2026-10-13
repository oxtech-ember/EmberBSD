/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Native C adaptation: see README.md and LICENSE in this directory. */

#ifndef PVR_ROGUE_FWIF_CLIENT_H
#define PVR_ROGUE_FWIF_CLIENT_H

#include "pvr_fw_abi.h"

#include "pvr_rogue_fwif_shared.h"

/*
 * Page size used for Parameter Management.
 */
#define ROGUE_PM_PAGE_SIZE 0x1000U

/*
 * Minimum/Maximum PB size.
 *
 * Base page size is dependent on core:
 *   S6/S6XT/S7               = 50 pages
 *   S8XE                     = 40 pages
 *   S8XE with BRN66011 fixed = 25 pages
 *
 * Minimum PB = Base Pages + (NUM_TE_PIPES-1)*16K + (NUM_VCE_PIPES-1)*64K +
 *              IF_PM_PREALLOC(NUM_TE_PIPES*16K + NUM_VCE_PIPES*16K)
 *
 * Maximum PB size must ensure that no PM address space can be fully used,
 * because if the full address space was used it would wrap and corrupt itself.
 * Since there are two freelists (local is always minimum sized) this can be
 * described as following three conditions being met:
 *
 *   (Minimum PB + Maximum PB)  <  ALIST PM address space size (16GB)
 *   (Minimum PB + Maximum PB)  <  TE PM address space size (16GB) / NUM_TE_PIPES
 *   (Minimum PB + Maximum PB)  <  VCE PM address space size (16GB) / NUM_VCE_PIPES
 *
 * Since the max of NUM_TE_PIPES and NUM_VCE_PIPES is 4, we have a hard limit
 * of 4GB minus the Minimum PB. For convenience we take the smaller power-of-2
 * value of 2GB. This is far more than any current applications use.
 */
#define ROGUE_PM_MAX_FREELIST_SIZE 0x80000000U

/*
 * Flags supported by the geometry DM command i.e. &struct rogue_fwif_cmd_geom.
 */

#define ROGUE_GEOM_FLAGS_FIRSTKICK PVR_FW_BIT(0)
#define ROGUE_GEOM_FLAGS_LASTKICK PVR_FW_BIT(1)
/* Use single core in a multi core setup. */
#define ROGUE_GEOM_FLAGS_SINGLE_CORE PVR_FW_BIT(3)

/*
 * Flags supported by the fragment DM command i.e. &struct rogue_fwif_cmd_frag.
 */

/* Use single core in a multi core setup. */
#define ROGUE_FRAG_FLAGS_SINGLE_CORE PVR_FW_BIT(3)
/* Indicates whether this render produces visibility results. */
#define ROGUE_FRAG_FLAGS_GET_VIS_RESULTS PVR_FW_BIT(5)
/* Indicates whether a depth buffer is present. */
#define ROGUE_FRAG_FLAGS_DEPTHBUFFER PVR_FW_BIT(7)
/* Indicates whether a stencil buffer is present. */
#define ROGUE_FRAG_FLAGS_STENCILBUFFER PVR_FW_BIT(8)
/* Disable pixel merging for this render. */
#define ROGUE_FRAG_FLAGS_DISABLE_PIXELMERGE PVR_FW_BIT(15)
/* Indicates whether a scratch buffer is present. */
#define ROGUE_FRAG_FLAGS_SCRATCHBUFFER PVR_FW_BIT(19)
/* Disallow compute overlapped with this render. */
#define ROGUE_FRAG_FLAGS_PREVENT_CDM_OVERLAP PVR_FW_BIT(26)

/*
 * Flags supported by the compute DM command i.e. &struct rogue_fwif_cmd_compute.
 */

#define ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP PVR_FW_BIT(2)
/*!< Use single core in a multi core setup. */
#define ROGUE_COMPUTE_FLAG_SINGLE_CORE PVR_FW_BIT(5)

/*
 * Flags supported by the transfer DM command i.e. &struct rogue_fwif_cmd_transfer.
 */

/*!< Use single core in a multi core setup. */
#define ROGUE_TRANSFER_FLAGS_SINGLE_CORE PVR_FW_BIT(1)

/*
 ************************************************
 * Parameter/HWRTData control structures.
 ************************************************
 */

/*
 * Configuration registers which need to be loaded by the firmware before a geometry
 * job can be started.
 */
struct rogue_fwif_geom_regs {
	uint64_t vdm_ctrl_stream_base;
	uint64_t tpu_border_colour_table;

	/* Only used when feature VDM_DRAWINDIRECT present. */
	uint64_t vdm_draw_indirect0;
	/* Only used when feature VDM_DRAWINDIRECT present. */
	uint32_t vdm_draw_indirect1;

	uint32_t ppp_ctrl;
	uint32_t te_psg;
	/* Only used when BRN 49927 present. */
	uint32_t tpu;

	uint32_t vdm_context_resume_task0_size;
	/* Only used when feature VDM_OBJECT_LEVEL_LLS present. */
	uint32_t vdm_context_resume_task3_size;

	/* Only used when BRN 56279 or BRN 67381 present. */
	uint32_t pds_ctrl;

	uint32_t view_idx;

	/* Only used when feature TESSELLATION present */
	uint32_t pds_coeff_free_prog;

	uint32_t padding;
};

/* Only used when BRN 44455 or BRN 63027 present. */
struct rogue_fwif_dummy_rgnhdr_init_geom_regs {
	uint64_t te_psgregion_addr;
};

/*
 * Represents a geometry command that can be used to tile a whole scene's objects as
 * per TA behavior.
 */
struct rogue_fwif_cmd_geom {
	/*
	 * rogue_fwif_cmd_geom_frag_shared field must always be at the beginning of the
	 * struct.
	 *
	 * The command struct (rogue_fwif_cmd_geom) is shared between Client and
	 * Firmware. Kernel is unable to perform read/write operations on the
	 * command struct, the SHARED region is the only exception from this rule.
	 * This region must be the first member so that Kernel can easily access it.
	 * For more info, see rogue_fwif_cmd_geom_frag_shared definition.
	 */
	struct rogue_fwif_cmd_geom_frag_shared cmd_shared;

	struct rogue_fwif_geom_regs regs PVR_FW_ALIGNED(8);
	uint32_t flags PVR_FW_ALIGNED(8);

	/*
	 * Holds the geometry/fragment fence value to allow the fragment partial render command
	 * to go through.
	 */
	struct rogue_fwif_ufo partial_render_geom_frag_fence;

	/* Only used when BRN 44455 or BRN 63027 present. */
	struct rogue_fwif_dummy_rgnhdr_init_geom_regs dummy_rgnhdr_init_geom_regs PVR_FW_ALIGNED(8);

	/* Only used when BRN 61484 or BRN 66333 present. */
	uint32_t brn61484_66333_live_rt;

	uint32_t padding;
};

/*
 * Configuration registers which need to be loaded by the firmware before ISP
 * can be started.
 */
struct rogue_fwif_frag_regs {
	uint32_t usc_pixel_output_ctrl;

#define ROGUE_MAXIMUM_OUTPUT_REGISTERS_PER_PIXEL 8U
	uint32_t usc_clear_register[ROGUE_MAXIMUM_OUTPUT_REGISTERS_PER_PIXEL];

	uint32_t isp_bgobjdepth;
	uint32_t isp_bgobjvals;
	uint32_t isp_aa;
	/* Only used when feature S7_TOP_INFRASTRUCTURE present. */
	uint32_t isp_xtp_pipe_enable;

	uint32_t isp_ctl;

	/* Only used when BRN 49927 present. */
	uint32_t tpu;

	uint32_t event_pixel_pds_info;

	/* Only used when feature CLUSTER_GROUPING present. */
	uint32_t pixel_phantom;

	uint32_t view_idx;

	uint32_t event_pixel_pds_data;

	/* Only used when BRN 65101 present. */
	uint32_t brn65101_event_pixel_pds_data;

	/* Only used when feature GPU_MULTICORE_SUPPORT or BRN 47217 present. */
	uint32_t isp_oclqry_stride;

	/* Only used when feature ZLS_SUBTILE present. */
	uint32_t isp_zls_pixels;

	/* Only used when feature ISP_ZLS_D24_S8_PACKING_OGL_MODE present. */
	uint32_t rgx_cr_blackpearl_fix;

	/* All values below the ALIGN(8) must be 64 bit. */
	pvr_fw_aligned_u64 isp_scissor_base;
	uint64_t isp_dbias_base;
	uint64_t isp_oclqry_base;
	uint64_t isp_zlsctl;
	uint64_t isp_zload_store_base;
	uint64_t isp_stencil_load_store_base;

	/*
	 * Only used when feature FBCDC_ALGORITHM present and value < 3 or feature
	 * FB_CDC_V4 present. Additionally, BRNs 48754, 60227, 72310 and 72311 must
	 * not be present.
	 */
	uint64_t fb_cdc_zls;

#define ROGUE_PBE_WORDS_REQUIRED_FOR_RENDERS 3U
	uint64_t pbe_word[8U][ROGUE_PBE_WORDS_REQUIRED_FOR_RENDERS];
	uint64_t tpu_border_colour_table;
	uint64_t pds_bgnd[3U];

	/* Only used when BRN 65101 present. */
	uint64_t pds_bgnd_brn65101[3U];

	uint64_t pds_pr_bgnd[3U];

	/* Only used when BRN 62850 or 62865 present. */
	uint64_t isp_dummy_stencil_store_base;

	/* Only used when BRN 66193 present. */
	uint64_t isp_dummy_depth_store_base;

	/* Only used when BRN 67182 present. */
	uint32_t rgnhdr_single_rt_size;
	/* Only used when BRN 67182 present. */
	uint32_t rgnhdr_scratch_offset;
};

struct rogue_fwif_cmd_frag {
	struct rogue_fwif_cmd_geom_frag_shared cmd_shared PVR_FW_ALIGNED(8);

	struct rogue_fwif_frag_regs regs PVR_FW_ALIGNED(8);
	/* command control flags. */
	uint32_t flags;
	/* Stride IN BYTES for Z-Buffer in case of RTAs. */
	uint32_t zls_stride;
	/* Stride IN BYTES for S-Buffer in case of RTAs. */
	uint32_t sls_stride;

	/* Only used if feature GPU_MULTICORE_SUPPORT present. */
	uint32_t execute_count;
};

/*
 * Configuration registers which need to be loaded by the firmware before CDM
 * can be started.
 */
struct rogue_fwif_compute_regs {
	uint64_t tpu_border_colour_table;

	/* Only used when feature CDM_USER_MODE_QUEUE present. */
	uint64_t cdm_cb_queue;

	/* Only used when feature CDM_USER_MODE_QUEUE present. */
	uint64_t cdm_cb_base;
	/* Only used when feature CDM_USER_MODE_QUEUE present. */
	uint64_t cdm_cb;

	/* Only used when feature CDM_USER_MODE_QUEUE is not present. */
	uint64_t cdm_ctrl_stream_base;

	uint64_t cdm_context_state_base_addr;

	/* Only used when BRN 49927 is present. */
	uint32_t tpu;
	uint32_t cdm_resume_pds1;

	/* Only used when feature COMPUTE_MORTON_CAPABLE present. */
	uint32_t cdm_item;

	/* Only used when feature CLUSTER_GROUPING present. */
	uint32_t compute_cluster;

	/* Only used when feature TPU_DM_GLOBAL_REGISTERS present. */
	uint32_t tpu_tag_cdm_ctrl;

	uint32_t padding;
};

struct rogue_fwif_cmd_compute {
	/* Common command attributes */
	struct rogue_fwif_cmd_common common PVR_FW_ALIGNED(8);

	/* CDM registers */
	struct rogue_fwif_compute_regs regs;

	/* Control flags */
	uint32_t flags PVR_FW_ALIGNED(8);

	/* Only used when feature UNIFIED_STORE_VIRTUAL_PARTITIONING present. */
	uint32_t num_temp_regions;

	/* Only used when feature CDM_USER_MODE_QUEUE present. */
	uint32_t stream_start_offset;

	/* Only used when feature GPU_MULTICORE_SUPPORT present. */
	uint32_t execute_count;
};

struct rogue_fwif_transfer_regs {
	/*
	 * All 32 bit values should be added in the top section. This then requires only a
	 * single RGXFW_ALIGN to align all the 64 bit values in the second section.
	 */
	uint32_t isp_bgobjvals;

	uint32_t usc_pixel_output_ctrl;
	uint32_t usc_clear_register0;
	uint32_t usc_clear_register1;
	uint32_t usc_clear_register2;
	uint32_t usc_clear_register3;

	uint32_t isp_mtile_size;
	uint32_t isp_render_origin;
	uint32_t isp_ctl;

	/* Only used when feature S7_TOP_INFRASTRUCTURE present. */
	uint32_t isp_xtp_pipe_enable;
	uint32_t isp_aa;

	uint32_t event_pixel_pds_info;

	uint32_t event_pixel_pds_code;
	uint32_t event_pixel_pds_data;

	uint32_t isp_render;
	uint32_t isp_rgn;

	/* Only used when feature GPU_MULTICORE_SUPPORT present. */
	uint32_t frag_screen;

	/* All values below the pvr_fw_aligned_u64 must be 64 bit. */
	pvr_fw_aligned_u64 pds_bgnd0_base;
	uint64_t pds_bgnd1_base;
	uint64_t pds_bgnd3_sizeinfo;

	uint64_t isp_mtile_base;
#define ROGUE_PBE_WORDS_REQUIRED_FOR_TQS 3
	/* TQ_MAX_RENDER_TARGETS * PBE_STATE_SIZE */
	uint64_t pbe_wordx_mrty[3U * ROGUE_PBE_WORDS_REQUIRED_FOR_TQS];
};

struct rogue_fwif_cmd_transfer {
	/* Common command attributes */
	struct rogue_fwif_cmd_common common PVR_FW_ALIGNED(8);

	struct rogue_fwif_transfer_regs regs PVR_FW_ALIGNED(8);

	uint32_t flags;

	uint32_t padding;
};

#include "pvr_rogue_fwif_client_check.h"

#endif /* PVR_ROGUE_FWIF_CLIENT_H */
