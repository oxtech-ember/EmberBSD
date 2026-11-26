/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control.h"
#include "store.h"

/* Network delivery owns this signal flag in the installed program. */
volatile sig_atomic_t stopping;

static struct eu_json *
json(const char *s)
{
	struct eu_json *j = eu_json_parse(s, strlen(s));
	assert(j);
	return j;
}

static void
plan(const char *db, const char *expect)
{
	struct packages list = {0};
	struct artifact artifacts[3] = {0};
	const char *names[] = {"app-1.0", "libdep-1.0", "other-1.0"};
	const char *roots[] = {"apps/app", "devel/libdep", "misc/other"};
	list.list = calloc(3, sizeof(*list.list));
	list.order = calloc(3, sizeof(*list.order));
	list.count = 3;
	assert(list.list && list.order);
	for (size_t i = 0; i < 3; i++) {
		struct package *p = list.list + i;
		p->name = names[i];
		p->root = roots[i];
		p->base = strndup(names[i], (size_t)(strchr(names[i], '-') - names[i]));
		p->artifact = &artifacts[i];
		p->conflicts = json("[]");
		artifacts[i].id = roots[i];
		artifacts[i].deps = json(i == 0 ? "[\"devel/libdep\"]" : "[]");
	}
	bool wrong = eu_equal(expect, "wrong");
	int r = eu_plan_packages(&list, db, "apps/app", wrong ? "app-2.0" : "app-1.0");
	if (wrong)
		assert(r < 0 && list.updates == 0);
	else if (eu_equal(expect, "new")) {
		assert(r == 0 && list.updates == 2);
		assert(eu_equal(list.order[0]->name, "libdep-1.0") &&
		       !list.order[0]->manual);
		assert(eu_equal(list.order[1]->name, "app-1.0") &&
		       list.order[1]->manual);
	} else if (eu_equal(expect, "promote")) {
		assert(r == 0 && list.updates == 1 && list.order[0]->manual &&
		       eu_equal(list.order[0]->name, "app-1.0"));
	} else {
		assert(eu_equal(expect, "manual") && r == 0 && list.updates == 0);
	}
	for (size_t i = 0; i < 3; i++)
		eu_json_free((struct eu_json *)(uintptr_t)artifacts[i].deps);
	eu_packages_free(&list);
}

static void
browse(const char *prefix, const char *db)
{
	struct eu_json *doc =
	    json("{\"repository\":\"netbsd-aarch64\",\"release_id\":\"r1\","
		 "\"packages\":[{\"pkgname\":\"libdep-1.0\",\"root\":\"devel/"
		 "libdep\",\"size\":77},{\"pkgname\":\"app-1.0\",\"root\":\"apps/"
		 "app\",\"size\":99}]}");
	struct eu_json *view = store_rows(doc, prefix, db);
	assert(view &&
	       eu_equal(text_field(view, "schema"), "emberbsd.catalog-index.v1") &&
	       eu_equal(text_field(view, "desktop_binding"), "installed-pkgdb"));
	const struct eu_json *counts = eu_get(view, "counts");
	assert(int_field(counts, "desktop") == 1 && int_field(counts, "total") == 2 &&
	       int_field(counts, "installed") == 2);
	const struct eu_json *row = eu_get(view, "packages")->child;
	assert(eu_equal(text_field(row, "root"), "apps/app") &&
	       eu_equal(text_field(row, "role"), "desktop"));
	assert(eu_equal(text_field(row->next, "role"), "library"));
	const struct eu_json *e = eu_get(row, "desktops")->child;
	assert(e && !e->next && eu_equal(text_field(e, "id"), "app") &&
	       eu_equal(text_field(e, "name"), "Viewer"));
	assert(eu_equal(text_field(eu_get(e, "names"), "ru"), "Обзор"));
	const struct eu_json *args = eu_get(e, "exec")->child;
	assert(args && strstr(eu_string(args), "/bin/application") &&
	       eu_equal(eu_string(args->next), "two words") && !args->next->next);
	assert(eu_equal(text_field(e, "source"), "installed-pkgdb") &&
	       eu_get(e, "icon")->type == EU_NULL);
	assert(eu_emit(stdout, view) == 0 && putchar('\n') != EOF);
	eu_json_free(view);
	eu_json_free(doc);
}
int
main(int argc, char **argv)
{
	assert(argc == 4);
	struct eu_json *profile =
	    json("{\"profile\":\"vm\",\"adapter\":\"none\",\"storage\":\"single\","
		 "\"target\":{\"arch\":\"aarch64\",\"triplet\":\"aarch64--netbsd\","
		 "\"abi\":\"netbsd-11\",\"runtime\":\"libc++\"}}");
	assert(eu_equal(kernel_mode(profile), "single"));
	eu_put(profile, "adapter", eu_text("custom"));
	assert(kernel_mode(profile) == NULL);
	eu_put(profile, "storage", eu_text("ffs"));
	eu_put(profile, "adapter", eu_text("efiboot"));
	assert(kernel_mode(profile) == NULL);
	eu_put(profile, "roots", json("{\"A\":\"root-a\",\"B\":\"root-b\"}"));
	eu_put(profile, "esp", eu_text("EFI"));
	assert(eu_equal(kernel_mode(profile), "slots"));
	eu_put(profile, "adapter", eu_text("uboot-ffs"));
	assert(eu_equal(kernel_mode(profile), "slots"));
	eu_json_free(profile);
	assert(!reboot_policy("install", false, "always", 0));
	assert(!reboot_policy("install", false, "00:00-23:59", 0));
	assert(reboot_policy("auto", false, "always", 0));
	assert(!reboot_policy("auto", false, "ask", 0));
	if (eu_equal(argv[1], "browse"))
		browse(argv[2], argv[3]);
	else
		plan(argv[2], argv[3]);
	return 0;
}
