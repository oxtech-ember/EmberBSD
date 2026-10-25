/* Derived from tools/compat/compat_defs.h, NetBSD revision 1.125.2.2. */
/* EmberBSD (AI-assisted): shared declarations for the unchanged efun closure. */
#ifndef EMBERBSD_COMPAT_EFUN_H
#define EMBERBSD_COMPAT_EFUN_H

/* The caller supplies native stdio, stdlib, inttypes and probe definitions. */
#ifdef __UNCONST
#undef __UNCONST
#endif
#define __UNCONST(a)   ((void *)(unsigned long)(const void *)(a))

#undef __predict_false
#define __predict_false(x) (x)

#undef __unused
#define __unused

#if !HAVE_ESETFUNC
void (*esetfunc(void (*)(int, const char *, ...)))(int, const char *, ...);
size_t estrlcpy(char *, const char *, size_t);
size_t estrlcat(char *, const char *, size_t);
char *estrdup(const char *);
char *estrndup(const char *, size_t);
void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void *, size_t);
FILE *efopen(const char *, const char *);
int easprintf(char **, const char *, ...);
int evasprintf(char **, const char *, va_list);
#endif

#if !HAVE_DECL_REALLOCARR
int reallocarr(void *, size_t, size_t);
#endif

#if !HAVE_DECL_STRTOI
intmax_t strtoi(const char * __restrict, char ** __restrict, int,
    intmax_t, intmax_t, int *);
#endif

#if !HAVE_DECL_STRTOU
uintmax_t strtou(const char * __restrict, char ** __restrict, int,
    uintmax_t, uintmax_t, int *);
#endif

/* Preserve the full compatibility layer's existing diagnostic-assert policy. */
#undef _DIAGASSERT
#define _DIAGASSERT(x)

#endif /* EMBERBSD_COMPAT_EFUN_H */
