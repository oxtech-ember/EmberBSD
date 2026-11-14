/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Signatures from the headers
 * of the 6.18 core and the calls of its MIT files.
 *
 * What the core calls and this system does not have:
 *
 *   - writeback connectors (drm_writeback.c is GPL-only and not rewritten):
 *     the calls are reached only with a writeback connector, which no
 *     driver of this system creates; they fail with -ENODEV (get the fence of
 *     none) and say so on the console, once.
 *   - the audio codec of an HDMI connector (the HDMI audio helper needs
 *     ALSA): there is no codec to tell about the plug.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/errno.h>

#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_writeback.h>

static volatile unsigned said;

static void
unavailable(const char *fn)
{

	if (atomic_swap_uint(&said, 1) == 0)
		printf("drm618: writeback connectors are not available yet: %s\n",
		    fn);
}

int
drm_writeback_prepare_job(struct drm_writeback_job *job __unused)
{

	unavailable(__func__);
	return -ENODEV;
}

void
drm_writeback_cleanup_job(struct drm_writeback_job *job __unused)
{

	unavailable(__func__);
}

int
drm_writeback_set_fb(struct drm_connector_state *conn_state __unused,
    struct drm_framebuffer *fb __unused)
{

	unavailable(__func__);
	return -ENODEV;
}

struct dma_fence *
drm_writeback_get_out_fence(struct drm_writeback_connector *wb_connector __unused)
{

	unavailable(__func__);
	return NULL;
}

void
drm_connector_hdmi_audio_plugged_notify(struct drm_connector *connector __unused,
    bool plugged __unused)
{

	/* No audio codec behind an HDMI connector here: nobody to tell. */
}
