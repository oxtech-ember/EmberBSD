/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>
#include <regexp.h>
#include <re_comp.h>

int
main(void)
{
	regexp *program;
	struct {
		unsigned char before[8];
		char value[64];
		unsigned char after[8];
	} output;
	void *allocation;
	size_t i;

	allocation = malloc(32);
	if (allocation == NULL)
		return (1);
	memset(allocation, 0x5a, 32);
	cfree(allocation);
	cfree(NULL);

	program = regcomp("^(ab|cd)+([0-9][0-9]*)$");
	if (program == NULL || regexec(program, "abcd42") != 1)
		return (2);
	memset(&output, 0xa5, sizeof(output));
	regsub(program, "\\2-&-\\1", output.value);
	if (strcmp(output.value, "42-abcd42-cd") != 0)
		return (3);
	for (i = 0; i < sizeof(output.before); i++)
		if (output.before[i] != 0xa5 || output.after[i] != 0xa5)
			return (4);
	if (regexec(program, "abcd") != 0 || regexec(program, "xx42") != 0)
		return (5);
	free(program);
	if (re_comp("^[a-z]+$") != NULL || re_exec("letters") != 1 ||
	    re_exec("123") != 0)
		return (6);
	if (re_comp(NULL) != NULL || re_comp("") != NULL ||
	    re_exec("letters") != 1)
		return (7);
	if (re_comp("(") == NULL)
		return (8);
	return (0);
}
