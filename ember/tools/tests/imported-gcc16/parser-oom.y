/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors. AI-assisted parser fixture.
 */
%pure-parser
%{
#include <assert.h>
#include <string.h>
#include "parser-under-test.h"
static const char *input;
static unsigned memory_errors;
static int yylex(int *
#ifdef YYLTYPE_IS_DECLARED
    , YYLTYPE *
#endif
);
static void yyerror(
#ifdef YYLTYPE_IS_DECLARED
    YYLTYPE *,
#endif
    const char *);
%}
%%
lines : /* empty */ | lines 'a' '\n' ;
%%
static int yylex(int *value
#ifdef YYLTYPE_IS_DECLARED
    , YYLTYPE *position
#endif
)
{
    *value = 0;
#ifdef YYLTYPE_IS_DECLARED
    memset(position, 0, sizeof(*position));
#endif
    return *input ? *input++ : 0;
}
static void yyerror(
#ifdef YYLTYPE_IS_DECLARED
    YYLTYPE *position,
#endif
    const char *message)
{
#ifdef YYLTYPE_IS_DECLARED
    (void)position;
#endif
    assert(strcmp(message, "memory exhausted") == 0);
    memory_errors++;
}
