/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), CTF type-layout regression. */
typedef unsigned long ember_word;
enum ember_mode { EMBER_NEGATIVE = -3, EMBER_READY = 7 };
union ember_value { long number; double real; };
struct ember_record {
	const char *name;
	ember_word values[3];
	enum ember_mode mode;
	union ember_value data;
	unsigned int ready : 1;
	unsigned int code : 7;
	unsigned int : 0;
	signed int delta : 9;
	struct ember_record *next;
	int (*callback)(struct ember_record *, int);
};
volatile struct ember_record ember_data;
int
ember_call(struct ember_record *r, int n)
{

	return r->callback(r, n);
}
_Static_assert(sizeof(struct ember_record) == 72, "record size");
_Static_assert(__builtin_offsetof(struct ember_record, callback) == 64,
    "callback offset");
