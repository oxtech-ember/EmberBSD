/*	$NetBSD$	*/

/*
 * Hand-written ioconf for the if_iwx module (cfdata for "iwx* at pci?").
 *
 * This mirrors what config(1) generates for IOCONF files, but the
 * component arrays are non-static: EmberBSD's bsd.kmodule.mk has no
 * IOCONF support, the file is compiled as its own object, and
 * if_iwx_modcmd() references these arrays from if_iwx.c.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/mount.h>

CFDRIVER_DECL(iwx, DV_IFNET, NULL);

struct cfdriver * const cfdriver_ioconf_if_iwx[] = {
	&iwx_cd,
	NULL
};

extern struct cfattach iwx_ca;

/* locators */
static int loc[2] = {
	-1, -1,
};

static const struct cfparent pspec0 = {
	"pci", "pci", DVUNIT_ANY
};

#define NORM FSTATE_NOTFOUND
#define STAR FSTATE_STAR

struct cfdata cfdata_ioconf_if_iwx[] = {
    /* driver           attachment    unit state      loc   flags  pspec */
/*  0: iwx* at pci? dev -1 function -1 */
    { "iwx",		"iwx",		 0, STAR, loc+  0,      0, &pspec0 },
    { NULL,		NULL,		 0,    0,    NULL,      0, NULL }
};

static struct cfattach * const iwx_cfattachinit[] = {
	&iwx_ca, NULL
};

const struct cfattachinit cfattach_ioconf_if_iwx[] = {
	{ "iwx", iwx_cfattachinit },
	{ NULL, NULL }
};
