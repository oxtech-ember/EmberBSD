/* Origin: EmberBSD external-ucom rump component, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/stat.h>
#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>
#include "ioconf.c"

RUMP_COMPONENT(RUMP_COMPONENT_DEV)
{
	extern const struct cdevsw ucom_cdevsw;
	devmajor_t cmaj = -1, bmaj = -1;

	FLAWLESSCALL(devsw_attach("ucom", NULL, &bmaj, &ucom_cdevsw, &cmaj));
	config_init_component(cfdriver_ioconf_ucom_mock,
	    cfattach_ioconf_ucom_mock, cfdata_ioconf_ucom_mock);
	FLAWLESSCALL(rump_vfs_makedevnodes(S_IFCHR, "/dev/ttyU", '0', cmaj, 0, 7));
	FLAWLESSCALL(rump_vfs_makedevnodes(S_IFCHR, "/dev/dtyU", '0', cmaj, 0x80000, 7));
}
