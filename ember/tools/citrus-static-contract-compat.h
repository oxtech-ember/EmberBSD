/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef CITRUS_TEST_COMPAT_H
#define CITRUS_TEST_COMPAT_H
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdalign.h>
#include <wchar.h>
#ifndef __CTASSERT
#define __CTASSERT(x) _Static_assert((x), #x)
#endif
#include <arpa/inet.h>
#ifndef _DIAGASSERT
#define _DIAGASSERT(x) assert(x)
#endif
#ifndef __RCSID
#define __RCSID(x)
#endif
#ifndef __weak_alias
#define __weak_alias(x,y)
#endif
#ifndef __UNCONST
#define __UNCONST(x) ((void *)(uintptr_t)(const void *)(x))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef __predict_false
#define __predict_false(x) (x)
#define __predict_true(x) (x)
#endif
#ifndef EFTYPE
#define EFTYPE 79
#endif
#ifndef MAP_FILE
#define MAP_FILE 0
#endif
#ifndef be32toh
#define be32toh(x) ntohl(x)
#define htobe32(x) htonl(x)
#define be16toh(x) ntohs(x)
#define htobe16(x) htons(x)
#endif
#ifndef __used
#define __used __attribute__((__used__))
#endif
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif
#ifndef __dead
#define __dead __attribute__((noreturn))
#endif
#ifndef __printflike
#define __printflike(a,b) __attribute__((format(printf,a,b)))
#endif
#define issetugid citrus_test_issetugid
int citrus_test_issetugid(void);
#undef strlcpy
size_t strlcpy(char *, const char *, size_t);
void *citrus_test_malloc(size_t);
void *citrus_test_calloc(size_t,size_t);
void *citrus_test_realloc(void *,size_t);
void citrus_test_free(void *);
char *citrus_test_strdup(const char *);
#endif
