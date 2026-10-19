/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors. AI-assisted parser fixture.
 */
%pure-parser
%{
#include <assert.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif
#include <stdio.h>
#include <string.h>
static const char *input;
static unsigned errors, accepted;
static int yylex(int *);
static void yyerror(const char *);
%}
%%
lines : /* empty */
      | lines 'a' '\n' { accepted++; }
      | lines error '\n' { yyerrok; }
      ;
%%
static int yylex(int *value)
{
    *value = 0;
    return *input ? *input++ : 0;
}
static void yyerror(const char *message)
{
    assert(strcmp(message, "syntax error") == 0);
    errors++;
}
int main(void)
{
    input = "a\na\n";
    int result = yyparse();
    assert(result == 0);
    assert(accepted == 2 && errors == 0);
    input = "x\na\n";
    result = yyparse();
    assert(result == 0);
    assert(accepted == 3 && errors == 1);
    puts("parser: valid input and syntax-error recovery pass");
    return 0;
}
