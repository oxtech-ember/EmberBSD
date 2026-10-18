/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted bounded actual-source regression. */
#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BINDIR "/fixture/bin"
#define REQUIRED_BY_FNAME "+REQUIRED_BY"
#define PRESERVE_FNAME "+PRESERVE"
#define INSTALLED_INFO_FNAME "+INSTALLED_INFO"

/* Only fields read by the two extracted functions are modeled. */
struct pkg_task {
	char *pkgname;
	const char *prefix;
	char *install_logdir;
	char *other_version;
	struct { char *meta_installed_info; } meta_data;
};

static int Fake, Verbose;
static const char *Destdir;
static const char *old_directory;
static unsigned int failures, cases, exec_calls;
static int exec_result;
static const char *case_name;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s: %s\n", case_name, message);
		failures++;
	}
}

static char *
xasprintf(const char *format, ...)
{
	va_list ap;
	char *result;
	int length;

	va_start(ap, format);
	length = vsnprintf(NULL, 0, format, ap);
	va_end(ap);
	if (length < 0)
		err(2, "vsnprintf");
	result = malloc((size_t)length + 1);
	if (result == NULL)
		err(2, "malloc");
	va_start(ap, format);
	if (vsnprintf(result, (size_t)length + 1, format, ap) != length)
		err(2, "vsnprintf");
	va_end(ap);
	return result;
}

static const char *
pkgdb_get_dir(void)
{
	return "/fixture/db";
}

static char *
pkgdb_pkg_file(const char *pkgname, const char *name)
{
	check(strcmp(pkgname, "fixture-1.0") == 0, "old package identity");
	return xasprintf("%s/%s", old_directory, name);
}

/* Capture the real call, including empty arguments, without executing it. */
static int
fexec_skipempty(const char *file, ...)
{
	const char *expected[] = { "-K", "/fixture/db", "-p", "/fixture/prefix",
	    Destdir ? "-P" : "", Destdir ? Destdir : "", "fixture-1.0" };
	va_list ap;
	size_t i;

	exec_calls++;
	check(strcmp(file, BINDIR "/pkg_delete") == 0, "delete executable");
	va_start(ap, file);
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		const char *argument = va_arg(ap, const char *);

		check(argument != NULL && strcmp(argument, expected[i]) == 0,
		    "delete argument");
	}
	check(va_arg(ap, const char *) == NULL, "delete argument terminator");
	va_end(ap);
	return exec_result;
}

#include "functions.inc"

static const char *metadata[] = {
	REQUIRED_BY_FNAME, PRESERVE_FNAME, INSTALLED_INFO_FNAME
};

static int
exists(const char *directory, const char *name)
{
	struct stat st;
	char *path = xasprintf("%s/%s", directory, name);
	int result = stat(path, &st) == 0;

	free(path);
	return result;
}

static void
make_file(const char *directory, const char *name)
{
	char *path = xasprintf("%s/%s", directory, name);
	FILE *file = fopen(path, "w");

	if (file == NULL)
		err(2, "fopen");
	if (fputs("fixture metadata\n", file) == EOF || fclose(file) != 0)
		err(2, "write fixture");
	free(path);
}

static void
run_case(const char *root, const char *name, int fake, int verbose,
    const char *destdir, int provided_info, int result, int blocked, int missing)
{
	struct pkg_task pkg;
	char *directory, *old, *new;
	size_t i;
	int actual, expected;

	case_name = name;
	cases++;
	directory = xasprintf("%s/%s", root, name);
	old = xasprintf("%s/old", directory);
	new = xasprintf("%s/new", directory);
	if (mkdir(directory, 0700) != 0 || mkdir(old, 0700) != 0 ||
	    mkdir(new, 0700) != 0)
		err(2, "mkdir fixture");
	for (i = 0; i < sizeof(metadata) / sizeof(metadata[0]); i++) {
		if (!missing)
			make_file(old, metadata[i]);
		if ((int)i == blocked) {
			char *path = xasprintf("%s/%s", new, metadata[i]);

			if (mkdir(path, 0700) != 0)
				err(2, "mkdir blocked metadata");
			make_file(path, "keep");
			free(path);
		}
	}
	pkg = (struct pkg_task) {
		.pkgname = "fixture-2.0", .prefix = "/fixture/prefix",
		.install_logdir = new, .other_version = "fixture-1.0",
		.meta_data = { .meta_installed_info = provided_info ? "provided" : NULL }
	};
	Fake = fake;
	Verbose = verbose;
	Destdir = destdir;
	old_directory = old;
	exec_result = result;
	exec_calls = 0;
	actual = start_replacing(&pkg);
	expected = blocked >= 0 && !(blocked == 2 && provided_info) ? -1 :
	    fake ? 0 : result;
	check(actual == expected, "return status");
	check(exec_calls == (unsigned int)(!fake &&
	    (blocked < 0 || (blocked == 2 && provided_info))), "delete call count");
	for (i = 0; i < sizeof(metadata) / sizeof(metadata[0]); i++) {
		int retained = fake || (i == 2 && provided_info) ||
		    (blocked >= 0 && (int)i >= blocked);

		check(exists(old, metadata[i]) == (!missing && retained),
		    "old metadata ownership");
		check(exists(new, metadata[i]) == ((int)i == blocked ||
		    (!missing && !retained)), "new metadata ownership");
	}
	printf("CASE: %s actual=%d expected=%d exec=%u\n", name, actual,
	    expected, exec_calls);
	free(new);
	free(old);
	free(directory);
}

int
main(int argc, char **argv)
{
	char name[96];
	int fake, verbose, provided, stage, status;
	const int statuses[] = { 0, -1, 7 };
	size_t i;

	if (argc != 2)
		errx(2, "Usage: fixture NEW_EXISTING_EMPTY_DIRECTORY");
	for (fake = 0; fake <= 1; fake++)
		for (verbose = 0; verbose <= 1; verbose++)
			for (provided = 0; provided <= 1; provided++)
				for (stage = 0; stage <= 1; stage++)
					for (i = 0; i < (fake ? 1 : sizeof(statuses) /
					    sizeof(statuses[0])); i++) {
						status = statuses[i];
						(void)snprintf(name, sizeof(name),
						    "fake%d-verbose%d-info%d-stage%d-status%u",
						    fake, verbose, provided, stage, (unsigned int)i);
						run_case(argv[1], name, fake, verbose,
						    stage ? "/fixture/stage" : NULL,
						    provided, status, -1, 0);
					}
	for (status = 0; status < 3; status++) {
		(void)snprintf(name, sizeof(name), "metadata-error-%d", status);
		run_case(argv[1], name, 0, 0, NULL, 0, 0, status, 0);
	}
	run_case(argv[1], "provided-info-skips-old-info", 0, 0, NULL, 1, 0, 2, 0);
	run_case(argv[1], "missing-metadata-is-allowed", 0, 0, NULL, 0, 0, -1, 1);
	printf("%s: %u actual-source cases; %u failed assertions\n",
	    failures ? "FAIL" : "PASS", cases, failures);
	return failures ? 1 : 0;
}
