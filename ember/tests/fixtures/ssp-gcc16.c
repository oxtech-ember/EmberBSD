/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted GCC stack-protector regression. */

extern void ssp_escape(void *);
void ssp_character_one(void);
void ssp_character_large(void);
void ssp_address_local(void);
long ssp_plain_local(long);
#ifndef OMIT_ZERO_ARRAY
void ssp_nested_zero(void);
#endif

struct ssp_zero_array {
	char empty[0];
	long value;
};

void
ssp_character_one(void)
{
	char data[1] = { 0 };

	ssp_escape(data);
}

void
ssp_character_large(void)
{
	char data[32] = { 0 };

	ssp_escape(data);
}

void
ssp_address_local(void)
{
	long value = 1;

	ssp_escape(&value);
}

#ifndef OMIT_ZERO_ARRAY
void
ssp_nested_zero(void)
{
	struct ssp_zero_array object = { .value = 1 };

	ssp_escape(&object);
}
#endif

long
ssp_plain_local(long value)
{

	return value + 1;
}
