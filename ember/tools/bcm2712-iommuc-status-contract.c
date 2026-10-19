/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual module body and contract types; controlled cached API services. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <platform/acenv.h>
#include <actypes.h>
#include <acexcep.h>
#include "iommuc.h"
#include "core.h"
#include "native.h"

static unsigned checks, stage, mode, printed, forbidden;
static unsigned char provider_storage;
static char message[512];
#define CHECK(v) do { checks++; if (!(v)) { \
	fprintf(stderr,"status CHECK %s:%d: %s\n",__FILE__,__LINE__,#v); \
	abort(); } } while (0)
typedef int modcmd_t;
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define MODULE_CLASS_MISC 7
#define MODULE(c,n,d) static const int module_class=(c); \
	static const char module_name[]=#n; static const void *module_dependency=(d)

static struct bcm2712_iommuc_provider *fixture_provider(void)
{ return (struct bcm2712_iommuc_provider *)&provider_storage; }
static ACPI_STATUS AcpiGetHandle(ACPI_HANDLE parent, ACPI_STRING path,
    ACPI_HANDLE *out)
{
	CHECK(stage++ == 0 && parent == NULL && out != NULL && *out == NULL);
	CHECK(strcmp(path,"\\_SB.ICAC") == 0);
	*out = (ACPI_HANDLE)(uintptr_t)0x1234;
	return mode == 1 ? AE_NOT_FOUND : AE_OK;
}
int bcm2712_iommuc_lookup(ACPI_HANDLE handle,
    struct bcm2712_iommuc_provider **out)
{
	CHECK(stage++ == 1 && mode != 1);
	CHECK(handle == (ACPI_HANDLE)(uintptr_t)0x1234 && out != NULL && *out == NULL);
	*out = fixture_provider();
	return mode == 2 ? EIO : 0;
}
int bcm2712_iommuc_cached_status(struct bcm2712_iommuc_provider *p,
    struct bcm2712_iommuc_native_status *out)
{
	struct bcm2712_iommuc_native_status zero;

	CHECK(stage++ == 2 && mode != 1 && mode != 2 && p == fixture_provider());
	memset(&zero,0,sizeof(zero));
	CHECK(out != NULL && memcmp(out,&zero,sizeof(zero)) == 0);
	if (mode == 3) { memset(out,0xa5,sizeof(*out)); return EAGAIN; }
	out->observation.raw_control = 0xa1b2c3d4;
	if (mode == 4) return 0;
	out->software_attached = true;
	out->core_initialized = true;
	out->observation.stage = BCM2712_IOMMUC_OBSERVE_READ;
	out->observation.started = true; out->observation.mapped = true;
	out->observation.raw_valid = true; out->observation.first_error = EOPNOTSUPP;
	out->observation.reads_attempted = 3; out->observation.reads_completed = 2;
	out->mmu4.attempted = true; out->mmu4.first_error = EBUSY; out->mmu4.fault = EFAULT;
	return 0;
}
int bcm2712_iommuc_observe_control(struct bcm2712_iommuc_provider *p)
{ (void)p; forbidden++; CHECK(forbidden == 0); return EIO; }
static void aprint_normal(const char *format, ...)
{
	va_list ap;

	CHECK(stage == 3 && mode != 1 && mode != 2 && mode != 3);
	printed++; va_start(ap,format);
	CHECK(vsnprintf(message,sizeof(message),format,ap) > 0);
	va_end(ap);
}
#include "module.h"

int main(void)
{
	char expected[512];

	CHECK(module_class == MODULE_CLASS_MISC && module_dependency == NULL);
	CHECK(strcmp(module_name,"bcm2712_iommuc_status") == 0);
	for (unsigned repeat=0; repeat<2; repeat++) {
		for (mode=0; mode<5; mode++) {
			int result;

			stage=printed=forbidden=0;memset(message,0,sizeof(message));
			CHECK(bcm2712_iommuc_status_modcmd(MODULE_CMD_FINI,&provider_storage) == 0);
			CHECK(bcm2712_iommuc_status_modcmd(99,&provider_storage) == ENOTTY && stage == 0);
			result=bcm2712_iommuc_status_modcmd(MODULE_CMD_INIT,&provider_storage);
			CHECK(forbidden == 0);
			if (mode>=1 && mode<=3) {
				CHECK(result == (mode==1 ? ENXIO : mode==2 ? EIO : EAGAIN));
				CHECK(stage == mode && printed == 0 && message[0] == 0);
			} else {
				CHECK(result == 0 && stage == 3 && printed == 1);
				CHECK(snprintf(expected,sizeof(expected),
				    "bcm2712_iommuc_status: cached only; attached %u, "
				    "core initialized %u; observation stage %u, started %u, mapped %u, "
				    "raw valid %u, CONTROL a1b2c3d4, first error %d, reads %u/%u; "
				    "MMU4 attempted %u, first error %d, fault %d; no new MMIO\n",
				    mode==0,mode==0,mode==0 ? 2U:0U,mode==0,mode==0,mode==0,
				    mode==0 ? EOPNOTSUPP:0,mode==0 ? 3U:0U,mode==0 ? 2U:0U,
				    mode==0,mode==0 ? EBUSY:0,mode==0 ? EFAULT:0) > 0);
				CHECK(strcmp(message,expected) == 0);
			}
			CHECK(bcm2712_iommuc_status_modcmd(MODULE_CMD_FINI,NULL) == 0);
		}
	}
	printf("cached ICAC status module: %u actual-source checks PASS\n",checks);
	return 0;
}
