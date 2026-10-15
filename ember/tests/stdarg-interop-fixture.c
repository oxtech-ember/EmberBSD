/* SPDX-License-Identifier: BSD-2-Clause */
#ifdef EXPECT_NO_COPY
#ifdef va_copy
#error ANSI fallback must not expose va_copy
#endif
#endif
/* All modes retain the internal copy operation, including ANSI and lint. */
int sum(int, ...);
int sum(int count, ...)
{
 va_list ap, copy, internal;
 @START@
 __va_copy(internal, ap);
#ifdef va_copy
 va_copy(copy, ap);
#else
 __va_copy(copy, ap);
#endif
 int total = 0;
 for (int i = 0; i < count; i++) total += va_arg(copy, int);
 int first = va_arg(ap, int);
 int internal_first = va_arg(internal, int);
 va_end(copy); va_end(internal); va_end(ap);
 return total + (first == 4 && internal_first == 4 ? 0 : 1000);
}
int mixed(int, ...);
int mixed(int count, ...)
{
 (void)count;
 va_list ap;
 va_start(ap, count);
 int i = va_arg(ap, int);
 double d = va_arg(ap, double);
 void *p = va_arg(ap, void *);
 va_end(ap);
 return i != 7 || d != 2.5 || p == 0;
}
int main(void)
{
 int sentinel;
 return sum(3, 4, 5, 6) != 15 || mixed(0, 7, 2.5, &sentinel) != 0;
}
