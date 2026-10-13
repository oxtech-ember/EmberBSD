/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted singular/plural catalog regression. */
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks, failures;

static void
check(const char *what, const char *actual, const char *expected)
{
	checks++;
	if (actual == NULL || strcmp(actual, expected) != 0) {
		fprintf(stderr, "%s: got <%s>, expected <%s>\n", what,
		    actual != NULL ? actual : "NULL", expected);
		failures++;
	}
}

int
main(int argc, char **argv)
{
	static const unsigned long counts[] = { 0, 1, 2, 3, 6, 11 };
	static const char *const forms[] = {
		"zero", "one", "two", "three", "six", "many"
	};
	const char *domain;
	size_t i;

	if (argc != 3)
		return 2;
	domain = argv[2];
	if (setenv("LANGUAGE", "cy", 1) != 0 ||
	    setlocale(LC_ALL, "C.UTF-8") == NULL ||
	    bindtextdomain(domain, argv[1]) == NULL ||
	    bind_textdomain_codeset(domain, "UTF-8") == NULL ||
	    textdomain(domain) == NULL)
		return 2;
	/* Welsh has plural(1) == 1, unlike the usual English rule. */
	check("gettext", gettext("Norway"), "Norwy");
	check("dgettext", dgettext(domain, "Norway"), "Norwy");
	check("dcgettext", dcgettext(domain, "Norway", LC_MESSAGES), "Norwy");
	check("pgettext", pgettext("country", "Norway"), "Context Norwy");
	check("dpgettext", dpgettext(domain, "country", "Norway"), "Context Norwy");
	check("dcpgettext", dcpgettext(domain, "country", "Norway", LC_MESSAGES), "Context Norwy");
	check("untranslated singular", dgettext(domain, "Missing"), "Missing");
	check("empty translation", dgettext(domain, "Empty"), "Empty");
	for (i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
		check("ngettext", ngettext("item", "items", counts[i]), forms[i]);
		check("dngettext", dngettext(domain, "item", "items", counts[i]), forms[i]);
		check("dcngettext", dcngettext(domain, "item", "items", counts[i], LC_MESSAGES), forms[i]);
		check("npgettext", npgettext("count", "item", "items", counts[i]), forms[i]);
		check("dnpgettext", dnpgettext(domain, "count", "item", "items", counts[i]), forms[i]);
		check("dcnpgettext", dcnpgettext(domain, "count", "item", "items", counts[i], LC_MESSAGES), forms[i]);
		check("untranslated plural", dngettext(domain, "absent", "absents", counts[i]),
		    counts[i] == 1 ? "absent" : "absents");
	}
	printf("%s: %u checks, %u failures\n", domain, checks, failures);
	return failures != 0;
}
