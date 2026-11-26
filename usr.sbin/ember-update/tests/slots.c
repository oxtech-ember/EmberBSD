/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the controller with firmware that keeps the T flag until restart. */
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control.h"

#define HASH "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define BAD "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define BOOTID "0123456789abcdef0123456789abcdef"
#define COMMIT "0123456789012345678901234567890123456789"
#define TARGET                                                                         \
	"{\"arch\":\"aarch64\",\"triplet\":\"aarch64--netbsd\",\"runtime\":\"libc++"   \
	"\",\"abi\":\"netbsd-11\"}"

static const char *status, *after, *attempt, *boot = BOOTID, *hash = HASH;
static const char *installed =
    "{\"schema\":\"" PLAN
    "\",\"os_generation\":5,\"profile\":\"board\",\"target\":" TARGET
    ",\"os_commit\":\"" COMMIT "\",\"os_update_sha256\":\"" HASH "\"}";
static char *confirmation, *recorded;
static int healthy = 1, confirms, cancels, restarts, checks, failed, arms;
static jmp_buf on_error;
struct eu_json *ui;
volatile sig_atomic_t stopping;

static struct eu_json *
json(const char *s)
{
	struct eu_json *j = eu_json_parse(s, strlen(s));
	assert(j);
	return j;
}
static char *
emit(const struct eu_json *v)
{
	char *s = NULL;
	size_t n = 0;
	FILE *f = open_memstream(&s, &n);
	assert(f && eu_emit(f, v) == 0 && fclose(f) == 0);
	return s;
}
const char *
text_field(const struct eu_json *j, const char *key)
{
	return eu_string(eu_get(j, key));
}
int64_t
int_field(const struct eu_json *j, const char *key)
{
	const struct eu_json *p = eu_get(j, key);
	return p && p->type == EU_INTEGER ? p->integer : -1;
}
void
trusted(const char *name, bool dir)
{
	(void)name;
	(void)dir;
}
void
checkpoint(void)
{
	if (stopping)
		longjmp(on_error, 2);
}
void
trial_health(void)
{
	checks++;
	if (!healthy)
		fail("health");
}
void
history(const char *kind, const char *detail)
{
	(void)kind;
	(void)detail;
}
void
report(const char *s, const char *err)
{
	(void)err;
	eu_put(ui, "status", eu_text(s));
}
void
fail(const char *s)
{
	(void)s;
	failed++;
	longjmp(on_error, 1);
}
int
mock_stat(const char *s, struct stat *st)
{
	(void)st;
	assert(strstr(s, "ember-update.boot"));
	return 0;
}
int
mock_unlink(const char *s)
{
	assert(strstr(s, "ember-update-confirm"));
	free(confirmation);
	confirmation = NULL;
	return 0;
}
int
file_sha(const char *path, char digest[65])
{
	strcpy(digest, strcmp(path, INSTALLED) == 0 ? hash : HASH);
	return 0;
}
struct eu_json *
read_record(const char *path, bool optional)
{
	if (!strcmp(path, ATTEMPT))
		return attempt ? json(attempt) : NULL;
	if (!strcmp(path, INSTALLED))
		return json(installed);
	if (!strcmp(path, PROFILE))
		return json("{\"profile\":\"board\"}");
	if (strstr(path, "ember-update.boot")) {
		struct eu_json *j = eu_new(EU_OBJECT);
		eu_put(j, "id", eu_text(boot));
		return j;
	}
	if (strstr(path, "ember-update-confirm"))
		return confirmation ? json(confirmation) : NULL;
	assert(optional);
	return NULL;
}
int
mock_save(const char *path, const struct eu_json *j, unsigned mode)
{
	assert(mode == 0600);
	if (strstr(path, "ember-update-confirm")) {
		free(confirmation);
		confirmation = emit(j);
	} else if (!strcmp(path, ATTEMPT)) {
		char *tmp = emit(j);
		free(recorded);
		recorded = tmp;
		attempt = recorded;
	}
	return 0;
}
int
mock_run(const char *const args[], char **output, size_t limit)
{
	(void)limit;
	if (!strcmp(args[0], "/sbin/shutdown")) {
		restarts++;
		return 0;
	}
	if (!strcmp(args[0], INSTALL))
		return 0;
	assert(!strcmp(args[0], BOOT));
	if (!strcmp(args[1], "status")) {
		*output = strdup(status);
		return 0;
	}
	if (!strcmp(args[1], "confirm"))
		confirms++;
	else if (!strcmp(args[1], "cancel"))
		cancels++;
	else if (!strcmp(args[1], "arm"))
		arms++;
	else
		assert(0);
	status = after;
	return 0;
}
static const char trial[] =
    "{\"running\":\"B\",\"confirmed\":\"A\",\"next\":null,\"trial\":true,\"prepared\":{"
    "\"slot\":\"B\",\"sha256\":\"" HASH "\"}}";
static const char confirmed[] =
    "{\"running\":\"B\",\"confirmed\":\"B\",\"next\":null,\"trial\":true,\"prepared\":{"
    "\"slot\":\"B\",\"sha256\":\"" HASH "\"}}";
static const char armed[] =
    "{\"running\":\"A\",\"confirmed\":\"A\",\"next\":\"B\",\"trial\":false,"
    "\"prepared\":{\"slot\":\"B\",\"sha256\":\"" HASH "\"}}";
static const char unarmed[] =
    "{\"running\":\"A\",\"confirmed\":\"A\",\"next\":null,\"trial\":false,\"prepared\":"
    "{\"slot\":\"B\",\"sha256\":\"" HASH "\"}}";
static const char persisted[] =
    "{\"slot\":\"B\",\"sha256\":\"" HASH "\",\"boot\":\"" BOOTID
    "\",\"profile\":\"board\",\"phase\":\"armed\",\"os_generation\":5}";
int
main(void)
{
	struct hardware hw = {
	    .name = "board", .commit = COMMIT, .generation = 5, .target = json(TARGET)};
	struct slots s;
	ui = eu_new(EU_OBJECT);
	status = trial;
	after = confirmed;
	slot_status(&s);
	hash = BAD;
	if (!setjmp(on_error)) {
		reconcile(&s, &hw, true);
		assert(0);
	}
	assert(failed == 1 && checks == 0 && confirms == 0);
	hash = HASH;
	healthy = 0;
	if (!setjmp(on_error)) {
		reconcile(&s, &hw, true);
		assert(0);
	}
	assert(failed == 2 && checks == 1 && confirms == 0);
	healthy = 1;
	assert(reconcile(&s, &hw, true));
	assert(confirms == 1);
	assert(!confirmation);
	slot_status(&s);
	assert(s.trial && !reconcile(&s, &hw, true) && confirms == 1);
	confirmation = strdup("{\"slot\":\"B\",\"sha256\":\"" HASH "\"}");
	assert(reconcile(&s, &hw, false));
	assert(confirms == 2 && !confirmation);
	status = armed;
	after = unarmed;
	slot_status(&s);
	attempt = persisted;
	assert(reconcile(&s, &hw, false));
	assert(restarts == 0);
	assert(reconcile(&s, &hw, true));
	assert(restarts == 1);
	boot = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
	assert(!reconcile(&s, &hw, true));
	assert(cancels == 1 && restarts == 1);
	slot_status(&s);
	assert(!reconcile(&s, &hw, true));
	/* A different prepared plan never triggers an arm or reboot. */
	status = armed;
	attempt = persisted;
	boot = BOOTID;
	slot_status(&s);
	strcpy(s.digest, BAD);
	if (!setjmp(on_error)) {
		reconcile(&s, &hw, true);
		assert(0);
	}
	assert(failed == 3 && arms == 0 && restarts == 1);
	return 0;
}
