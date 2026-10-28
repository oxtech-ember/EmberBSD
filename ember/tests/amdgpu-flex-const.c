/* Exercise the actual PowerPlay helpers with mutable and const tables. */
#include "pp_debug.h"

struct entry { int value; };
struct table { struct entry entries[2]; };

int
amdgpu_flex_const_test(struct table *output, const struct table *input)
{
	struct entry *writable = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
	    struct entry, entries, output, 1);
	const struct entry *readable = GET_CONST_FLEXIBLE_ARRAY_MEMBER_ADDR(
	    struct entry, entries, input, 1);
#ifdef TEST_CONST_REJECTION
	struct entry *invalid = GET_CONST_FLEXIBLE_ARRAY_MEMBER_ADDR(
	    struct entry, entries, input, 1);
	return invalid->value;
#endif
	writable->value = readable->value;
	return writable->value;
}
