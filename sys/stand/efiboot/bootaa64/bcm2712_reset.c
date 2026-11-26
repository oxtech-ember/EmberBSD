/* SPDX-License-Identifier: BSD-2-Clause */
/* BCM2712 PM reset provider for a confirmed CM5 ACPI firmware binding. */

#include "../efiboot.h"
#include "../efiacpi.h"
#include "../efislot.h"

#include <arch/arm/broadcom/bcm2835_pmreg.h>

const char *
efi_md_trial_reset_provider(const char *requested, size_t len)
{
	static const char provider[] = "bcm2712-pm";

	if (len == sizeof(provider) - 1 &&
	    memcmp(requested, provider, len) == 0 && efi_acpi_available() &&
	    strcmp(efi_acpi_get_model(),
	    "Raspberry Pi Foundation Raspberry Pi Compute Module 5") == 0)
		return provider;
	return NULL;
}

bool
efi_md_trial_reset_arm(void)
{
	volatile uint32_t * const base = (void *)(uintptr_t)BCM2712_PM_BASE;
	volatile uint32_t * const reset = base + BCM2835_PM_RSTC / sizeof(*base);
	volatile uint32_t * const count = base + BCM2835_PM_WDOG / sizeof(*base);
	uint32_t step = *reset & ~(BCM2835_PM_PASSWORD_MASK | BCM2835_PM_RSTC_CONFIGMASK);

	*count = BCM2835_PM_PASSWORD | (BCM2835_PM_DEFAULT_PERIOD << 16);
	*reset = BCM2835_PM_PASSWORD | step | BCM2835_PM_RSTC_FULL_RESET;
	__asm volatile("dsb sy" ::: "memory");
	return (*reset & BCM2835_PM_RSTC_CONFIGMASK) == BCM2835_PM_RSTC_FULL_RESET &&
	    (*count & BCM2835_PM_WDOG_TIMEMASK) != 0;
}
