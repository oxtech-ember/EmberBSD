/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise real OpenPAM configuration, dlopen and session environment. */
#include <sys/types.h>
#include <sys/stat.h>
#include <security/pam_appl.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
static int conversation(int n, const struct pam_message **m,
			struct pam_response **r, void *data)
{
	(void)n;
	(void)m;
	(void)r;
	(void)data;
	return PAM_CONV_ERR;
}
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);   \
			exit(1);                                               \
		}                                                              \
	} while (0)
int main(void)
{
	struct pam_conv conv = {conversation, NULL};
	pam_handle_t *h = NULL;
	struct passwd *pw;
	struct stat st;
	char expected[256], dir[256];
	pw = getpwnam("nobody");
	CHECK(pw != NULL && pw->pw_uid != 0);
	snprintf(dir, sizeof(dir), "/tmp/ember-pam-xdg-pam-runtime/%lu",
		 (unsigned long)pw->pw_uid);
	snprintf(expected, sizeof(expected), "%s", dir);
	CHECK(pam_start("ember-pam-xdg-test", "nobody", &conv, &h) ==
	      PAM_SUCCESS);
	CHECK(pam_open_session(h, 0) == PAM_SUCCESS);
	CHECK(pam_getenv(h, "XDG_RUNTIME_DIR") != NULL &&
	      !strcmp(pam_getenv(h, "XDG_RUNTIME_DIR"), expected));
	CHECK(stat(dir, &st) == 0 && st.st_uid == pw->pw_uid &&
	      st.st_gid == pw->pw_gid && (st.st_mode & 07777) == 0700);
	CHECK(pam_close_session(h, 0) == PAM_SUCCESS);
	CHECK(access(dir, F_OK) != 0);
	CHECK(pam_getenv(h, "XDG_RUNTIME_DIR") == NULL);
	CHECK(pam_open_session(h, 0) == PAM_SUCCESS);
	CHECK(pam_getenv(h, "XDG_RUNTIME_DIR") != NULL &&
	      !strcmp(pam_getenv(h, "XDG_RUNTIME_DIR"), expected));
	CHECK(pam_close_session(h, 0) == PAM_SUCCESS);
	CHECK(pam_getenv(h, "XDG_RUNTIME_DIR") == NULL);
	CHECK(access(dir, F_OK) != 0);
	CHECK(pam_end(h, PAM_SUCCESS) == PAM_SUCCESS);
	puts("PASS real OpenPAM module load, session and XDG_RUNTIME_DIR");
	return 0;
}
