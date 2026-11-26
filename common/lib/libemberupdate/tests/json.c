/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "json.h"

int
main(void)
{
	const char *invalid[] = {"{}{}",
				 "{\"a\":1,\"\\u0061\":3}",
				 "[null,]",
				 "[1 2]",
				 "[truefalse]",
				 "[01]",
				 "{\"x\":-}",
				 "{\"x\"3}",
				 "{\"a\\u0000\":2}",
				 "[9223372036854775808]",
				 "[\"\\ud800\"]",
				 "[\"\\udfff\"]",
				 "[1.5]",
				 "[\"\xc0\x80\"]",
				 "[\"raw\nline\"]",
				 "[] :[]"};
	const char *text =
	    "{\"symbols\":[\"\\ud83c\\udf11\",false,null],"
	    "\"minimum\":-9223372036854775808,\"maximum\":9223372036854775807}";
	struct eu_json *doc;
	const struct eu_json *array;
	size_t i;
	assert(eu_json_parse(NULL, 7) == NULL);
	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		assert(eu_json_parse(invalid[i], strlen(invalid[i])) == NULL);
	doc = eu_json_parse(text, strlen(text));
	assert(doc != NULL && eu_get(doc, "minimum")->integer == INT64_MIN);
	assert(eu_get(doc, "maximum")->integer == INT64_MAX);
	array = eu_get(doc, "symbols");
	assert(array && array->type == EU_ARRAY);
	assert(strcmp(eu_string(array->child), "\xf0\x9f\x8c\x91") == 0);
	assert(array->child->next->type == EU_BOOL && array->child->next->integer == 0);
	assert(array->child->next->next->type == EU_NULL);
	eu_json_free(doc);

	/* Package catalogs can contain more elements than the object key limit. */
	char catalog[16384];
	size_t used = (size_t)snprintf(catalog, sizeof(catalog), "{\"packages\":[");
	for (i = 0; i < 256; i++)
		used +=
		    (size_t)snprintf(catalog + used, sizeof(catalog) - used,
				     "%s{\"pkgname\":\"app-%zu\"}", i ? "," : "", i);
	memcpy(catalog + used, "]}", 2);
	doc = eu_json_parse(catalog, used + 2);
	assert(doc && eu_get(doc, "packages")->type == EU_ARRAY);
	size_t count = 0;
	for (const struct eu_json *p = eu_get(doc, "packages")->child; p; p = p->next)
		count++;
	assert(count == 256);
	eu_json_free(doc);
	return 0;
}
