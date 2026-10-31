/* SPDX-License-Identifier: BSD-2-Clause */

/* Logging bridge for the shared WPA crypto sources. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "../esp_wifi_os.h"

void	wpa_printf(int, const char *, ...);
void	wpa_hexdump(int, const char *, const void *, size_t);
void	wpa_hexdump_key(int, const char *, const void *, size_t);

/* MSG_INFO is the upstream default threshold. */
#define	WPA_LOG_INFO	3

void
wpa_printf(int level, const char *fmt, ...)
{
	va_list ap;

	if (level < WPA_LOG_INFO)
		return;
	va_start(ap, fmt);
	espwifi_os_vlog(fmt, ap);
	va_end(ap);
	espwifi_os_log("\n");
}

void
wpa_hexdump(int level, const char *title, const void *buffer, size_t len)
{
	const uint8_t *p = buffer;

	if (level < WPA_LOG_INFO)
		return;
	espwifi_os_log("%s (%lu bytes):", title, (unsigned long)len);
	if (p == NULL)
		espwifi_os_log(" [NULL]");
	else
		for (size_t i = 0; i < len; i++)
			espwifi_os_log(" %02x", p[i]);
	espwifi_os_log("\n");
}

void
wpa_hexdump_key(int level, const char *title, const void *buffer, size_t len)
{
	(void)buffer;
	if (level >= WPA_LOG_INFO)
		espwifi_os_log("%s (%lu bytes): [redacted]\n", title,
		    (unsigned long)len);
}
