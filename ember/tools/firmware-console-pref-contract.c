/* Origin: EmberBSD RP1 console preference regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATIC static
#define VOID void
#define EFIAPI
#define IN
#define EFI_ERROR(status) ((status) != 0)
#define DEBUG(message) ((void)0)
#define CONSOLE_PREF_SERIAL 1
#define CONSOLE_PREF_VARIABLE_NAME name

typedef int EFI_STATUS;
typedef void *EFI_EVENT;
typedef size_t UINTN;
typedef struct {
	uint8_t Console;
	uint8_t Reserved[3];
} CONSOLE_PREF_VARSTORE_DATA;

static unsigned int checks, reads, gop_queries, dt_removes, spcr_removes;
static EFI_STATUS variable_error, gop_error;
static uint8_t preference;
static const uint16_t name[] = {'C', 'o', 'n', 's', 'o', 'l', 'e', 'P', 'r',
    'e', 'f', 0};
static int gConsolePrefFormSetGuid, gEfiGraphicsOutputProtocolGuid;

#define CHECK(expr) do { \
	checks++; \
	if (!(expr)) { \
		fprintf(stderr, "console preference CHECK %u: %s\n", \
		    __LINE__, #expr); \
		abort(); \
	} \
} while (0)

static EFI_STATUS
get_variable(const uint16_t *variable, const int *guid, void *attributes,
    UINTN *size, void *data)
{
	CONSOLE_PREF_VARSTORE_DATA value = {0};

	CHECK(variable == name && guid == &gConsolePrefFormSetGuid);
	CHECK(attributes == NULL && *size == sizeof(value));
	reads++;
	if (variable_error != 0)
		return variable_error;
	value.Console = preference;
	memcpy(data, &value, sizeof(value));
	return 0;
}

static EFI_STATUS
locate_protocol(const int *guid, void *registration, void **out)
{

	CHECK(guid == &gEfiGraphicsOutputProtocolGuid);
	CHECK(registration == NULL && reads == 1);
	gop_queries++;
	if (gop_error != 0)
		return gop_error;
	*out = &gEfiGraphicsOutputProtocolGuid;
	return 0;
}

static const struct {
	EFI_STATUS (*GetVariable)(const uint16_t *, const int *, void *,
	    UINTN *, void *);
} runtime = { get_variable }, *gRT = &runtime;
static const struct {
	EFI_STATUS (*LocateProtocol)(const int *, void *, void **);
} boot = { locate_protocol }, *gBS = &boot;

static void
RemoveDtStdoutPath(void)
{

	CHECK(reads == 1 && gop_queries == 1);
	dt_removes++;
}

static void __attribute__((__unused__))
RemoveSpcrTable(void)
{

	CHECK(reads == 1 && gop_queries == 1 && dt_removes == 1);
	spcr_removes++;
}

#include "console-pref-body.h"

int
main(void)
{
	unsigned int v, g, p;
	bool changes;

	for (v = 0; v != 2; v++) {
		for (g = 0; g != 2; g++) {
			for (p = 0; p != 2; p++) {
				reads = gop_queries = dt_removes = spcr_removes = 0;
				variable_error = (EFI_STATUS)v;
				gop_error = (EFI_STATUS)g;
				preference = (uint8_t)p;
				OnReadyToBoot(NULL, NULL);
				CHECK(reads == 1);
				CHECK(gop_queries == (unsigned int)(v == 0 && p == 0));
				changes = v == 0 && g == 0 && p == 0;
				CHECK(dt_removes == (unsigned int)changes);
#ifdef RP1_UART_CONSOLE
				CHECK(spcr_removes == 0);
#else
				CHECK(spcr_removes == (unsigned int)changes);
#endif
			}
		}
	}
	printf("console preference: %u checks passed\n", checks);
	return 0;
}
