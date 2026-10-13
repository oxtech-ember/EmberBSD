#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Execute the real host lex -> m4 -> generated scanner chain.
set -eu
[ "$#" = 4 ] || { echo 'usage: lex-host-filter.sh LEX HOST_CC SDK NEW_WORK' >&2; exit 2; }
lex=$1 cc=$2 sdk=$3 work=$4
[ ! -e "$work" ] || exit 2
mkdir "$work"
cat > "$work/input.l" <<'LEX'
%option noyywrap
%{
#include <stdio.h>
%}
%%
[a-z]+  { printf("word:%s\n", yytext); }
\n      { }
.       { return 1; }
%%
int main(void) { return yylex(); }
LEX
"$lex" -o "$work/scanner.c" "$work/input.l"
SDKROOT="$sdk" "$cc" "$work/scanner.c" -o "$work/scanner"
printf 'test\n' | "$work/scanner" > "$work/result"
printf 'word:test\n' > "$work/expected"
cmp "$work/expected" "$work/result"
echo 'PASS actual lex/m4 filter and generated scanner execution'
