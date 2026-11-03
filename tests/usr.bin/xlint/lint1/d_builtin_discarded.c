/* $NetBSD$ */
# 3 "d_builtin_discarded.c"

/* Synthesized builtin calls in discarded branches are not unused externs. */
/* lint1-flags: -g -w -X 351 */

int
left(unsigned long long n)
{
	return sizeof(n) == sizeof(unsigned long long)
	    ? 0 : __builtin_clzll(n);
}

int
right(unsigned long long n)
{
	return sizeof(n) != sizeof(unsigned long long)
	    ? __builtin_ctzll(n) : 0;
}

void
ordinary(void)
{
	/* expect+2: warning: nested 'extern' declaration of 'explicit_unused' [352] */
	/* expect+1: warning: 'explicit_unused' unused in function 'ordinary' [192] */
	extern int explicit_unused(int);
}
