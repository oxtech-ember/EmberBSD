/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise production unwind against a real OpenPAM handle. */
#include <sys/types.h>
#include <sys/stat.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int set_data_calls, saw_expected;
static char expected[256];

static int
reject_set_data(pam_handle_t *p, const char *k, void *d,
    void (*cleanup)(pam_handle_t *, void *, int))
{
	const char *value = pam_getenv(p, "XDG_RUNTIME_DIR");

	(void)k;
	(void)d;
	(void)cleanup;
	set_data_calls++;
	saw_expected = value != NULL && strcmp(value, expected) == 0;
	return PAM_BUF_ERR;
}
#if !defined(XDG_CONTROL) || !defined(XDG_RUNTIME)
#error "Select unique XDG_CONTROL and XDG_RUNTIME test paths"
#endif
#define pam_set_data reject_set_data
#include "../pam_xdg.c"
#undef pam_set_data

static int
conversation(int n, const struct pam_message **m,
    struct pam_response **r, void *d)
{
	(void)n;
	(void)m;
	(void)r;
	(void)d;
	return PAM_CONV_ERR;
}
#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
		exit(1); \
	} \
} while (0)

int
main(void)
{
	struct pam_conv conv = { conversation, NULL };
	pam_handle_t *h = NULL;
	struct passwd *pw;
	struct stat st;

	CHECK(geteuid() == 0);
	CHECK(lstat(XDG_CONTROL, &st) == -1 && errno == ENOENT);
	CHECK(lstat(XDG_RUNTIME, &st) == -1 && errno == ENOENT);
	pw = getpwnam("nobody");
	CHECK(pw != NULL && pw->pw_uid != 0);
	CHECK(snprintf(expected, sizeof(expected), "%s/%lu", XDG_RUNTIME,
	    (unsigned long)pw->pw_uid) < (int)sizeof(expected));
	CHECK(pam_start("ember-pam-xdg-unwind", "nobody", &conv, &h) ==
	    PAM_SUCCESS);
	CHECK(pam_sm_open_session(h, 0, 0, NULL) == PAM_SESSION_ERR);
	CHECK(set_data_calls == 1 && saw_expected);
	CHECK(pam_getenv(h, "XDG_RUNTIME_DIR") == NULL);
	CHECK(lstat(expected, &st) == -1 && errno == ENOENT);
	CHECK(pam_end(h, PAM_SUCCESS) == PAM_SUCCESS);
	puts("PASS real OpenPAM unwind");
	return 0;
}
