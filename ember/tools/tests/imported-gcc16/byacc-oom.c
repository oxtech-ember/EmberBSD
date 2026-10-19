/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors. AI-assisted allocation fixture.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif

static void *owned[8];
static unsigned calls, fail_at, live;

static void *
fault_malloc(size_t size)
{
    if (++calls == fail_at)
        return NULL;
    void *p = malloc(size);
    assert(p != NULL);
    for (unsigned i = 0; i < 8; i++) {
        if (owned[i] == NULL) {
            owned[i] = p;
            live++;
            return p;
        }
    }
    abort();
}

static void
checked_free(void *p)
{
    if (p == NULL)
        return;
    for (unsigned i = 0; i < 8; i++) {
        if (owned[i] == p) {
            owned[i] = NULL;
            live--;
            free(p);
            return;
        }
    }
    /* An uninitialized pointer or a duplicate free violates ownership. */
    abort();
}

#define malloc fault_malloc
#define free checked_free
#include "parser-under-test.c"
#undef free
#undef malloc

int
main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "parse") == 0) {
        fail_at = 1;
        input = "a\n";
        int result = yyparse();
        assert(result == 2 && memory_errors == 1 && calls == 1);
    } else if (strcmp(argv[1], "zero") == 0) {
        YYParseState *p = yyNewState(0);
        assert(p != NULL && calls == 1);
        assert(p->yystack.s_base == NULL && p->yystack.l_base == NULL);
#ifdef YYLTYPE_IS_DECLARED
        assert(p->yystack.p_base == NULL);
#endif
        yyFreeState(p);
    } else {
        unsigned count = 3;
#ifdef YYLTYPE_IS_DECLARED
        count++;
#endif
        fail_at = (unsigned)atoi(argv[1]);
        assert(fail_at <= count);
        YYParseState *p = yyNewState(8);
        if (fail_at != 0) {
            assert(p == NULL && calls == fail_at);
        } else {
            assert(p != NULL && calls == count);
            yyFreeState(p);
        }
    }
    assert(live == 0);
    return 0;
}
