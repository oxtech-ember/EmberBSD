/*	$NetBSD$	*/
# 3 "d_decl_parameter_recovery.c"

/* Keep unused-parameter diagnostics for valid declarations during recovery. */
/* lint1-extra-flags: -X 351 */

void
valid(
    /* expect+1: warning: parameter 'unused' unused in function 'valid' [231] */
    int unused)
{
}

/* An unexpanded alias can resemble an incomplete old-style definition. */
/* expect+1: warning: parameter '__ffssi2' unused in function '__weak_alias' [231] */
__weak_alias(__ffssi2, ffs)
/* expect+3: error: syntax error '{' [249] */
/* expect+2: warning: type of parameter '__ffssi2' defaults to 'int' [32] */
/* expect+1: error: 'mask' undefined [99] */
int ffs(int mask) { return mask; }
