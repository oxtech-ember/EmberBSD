/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: CIX boot profile contract with actual loader bodies, AI-assisted. */
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "queue-native.h"
#include "bootcfg-native.h"
#include "bootcfg-defines.inc"

#define uimin(a, b) ((a) < (b) ? (a) : (b))
#define COMMAND_SEPARATOR ';'
#define DEFFILENAME "netbsd"
static const char *bootfile = "netbsd";
#include "globals-native.inc"
static unsigned checks, failures, sequence, correction, boots, queued;
static char selected_kernel[256], selected_args[256], selected_initrd[255];
static void *allocations[128];
static unsigned nallocations;
struct boot_command {
	const char *c_name;
	void (*c_fn)(char *);
	const char *c_help;
};
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #e); } } while (0)

static void
panic(const char *msg)
{

	fprintf(stderr, "%s\n", msg);
	exit(2);
}

static void *
loader_alloc(size_t size)
{
	void *p = malloc(size);

	if (p == NULL || nallocations == 128)
		panic("loader test allocation failed");
	allocations[nallocations++] = p;
	return p;
}
#define alloc loader_alloc

/* Only EFI services and kernel execution are doubles. The real queue is used. */
static void
efi_acpi_cix_spcr(void)
{

	correction = ++sequence;
}

static void efi_acpi_enable(int enabled) { (void)enabled; }
static int efi_acpi_enabled(void) { return 1; }
static void module_add(const char *name) { (void)name; }
static void efi_block_set_readahead(bool enabled) { (void)enabled; }
static void command_help(char *arg) { (void)arg; failures++; }

#include "userconf-native.inc"

static void
check_queued(const char *cmd)
{

	CHECK(strcmp(cmd, "disable acpibat") == 0);
	queued++;
}

static int
exec_netbsd(const char *kernel, const char *args)
{

	/* This is the loader-to-kernel boundary, not a kernel/EFI boot. */
	CHECK(correction == 1 && ++sequence == 2);
	queued = 0;
	userconf_foreach(check_queued);
	CHECK(queued == 1);
	(void)snprintf(selected_kernel, sizeof(selected_kernel), "%s", kernel);
	(void)snprintf(selected_args, sizeof(selected_args), "%s", args);
	(void)snprintf(selected_initrd, sizeof(selected_initrd), "%s", initrd_path);
	boots++;
	return EIO;
}

#include "commands-native.inc"
#include "table-native.inc"
#include "dispatch-native.inc"
#include "config-native.inc"
#include "menu-native.inc"

static void
check_command_shape(const char *cmd)
{
	size_t len;

	/* The unchanged loader has an 80-byte array and unsafe empty segments. */
	CHECK(cmd != NULL);
	if (cmd == NULL)
		return;
	CHECK(cmd[0] != '\0' && cmd[strlen(cmd) - 1] != ';');
	do {
		len = strcspn(cmd, ";");
		CHECK(len > 0 && len < 80);
		cmd += len;
		if (*cmd != ';')
			break;
		cmd++;
	} while (*cmd != '\0');
}

int
main(int argc, char **argv)
{
	char empty[] = "";
	unsigned choice;

	if (argc != 3)
		return 2;
	CHECK(parsebootconf(argv[1]) == 0);
	CHECK(bootcfg_info.nummenu == 3);
	CHECK(bootcfg_info.timeout > 0 && bootcfg_info.timeout <= 10);
	CHECK(bootcfg_info.def >= 0 && bootcfg_info.def < bootcfg_info.nummenu);
	if (failures != 0)
		goto out;
	check_command_shape(bootcfg_info.command[0]);
	check_command_shape(bootcfg_info.command[1]);
	if (failures != 0)
		goto out;
	choice = strcmp(argv[2], "normal") == 0 ?
	    (unsigned)getchoicefrominput(empty, bootcfg_info.def) : 1;
	CHECK(bootcfg_info.def == 0);
	CHECK(strcmp(bootcfg_info.command[2], "prompt") == 0);
	/* A previous menu selection can leave an initrd path in this loader. */
	CHECK(set_initrd_path("stale-root.fs") == 0);
	docommandchoice((int)choice);
	CHECK(boots == 1);
	if (strcmp(argv[2], "normal") == 0) {
		CHECK(strcmp(selected_kernel, "netbsd.wifi.gz") == 0);
		CHECK(strcmp(selected_args, "-v root=NAME=ember-cix-root") == 0);
		CHECK(selected_initrd[0] == '\0');
	} else {
		CHECK(strcmp(selected_kernel, "/netbsd") == 0);
		CHECK(strcmp(selected_args, "-s") == 0);
		CHECK(strcmp(selected_initrd, "root.fs") == 0);
	}
out:
	/* The real loader keeps its menu/queue allocations until reset. */
	while (nallocations != 0)
		free(allocations[--nallocations]);
	printf("CIX %s boot: %u checks, %u failures\n", argv[2], checks, failures);
	return failures != 0;
}
