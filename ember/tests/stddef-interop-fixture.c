/* SPDX-License-Identifier: BSD-2-Clause */
#ifdef __cplusplus
#define CHECK(c) static_assert(c, #c)
#define ALIGN(t) alignof(t)
#else
#define CHECK(c) _Static_assert(c, #c)
#define ALIGN(t) _Alignof(t)
#endif
CHECK(sizeof(size_t) == sizeof(__SIZE_TYPE__));
CHECK(sizeof(ptrdiff_t) == sizeof(__PTRDIFF_TYPE__));
CHECK(sizeof(wchar_t) == sizeof(__WCHAR_TYPE__));
CHECK((size_t)-1 > 0);
CHECK((ptrdiff_t)-1 < 0);
struct offsets {
	char lead;
	int data[3];
};
CHECK(offsetof(struct offsets, data[2]) == __builtin_offsetof(struct offsets, data[2]));
#if (__STDC_VERSION__ - 0) >= 201112L || (__cplusplus - 0) >= 201103L
#if EXPECT_PROVIDER
#if !defined(_GCC_MAX_ALIGN_T) && !defined(__CLANG_MAX_ALIGN_T_DEFINED)
#error expected a genuine compiler max_align_t provider
#endif
#else
#if defined(_GCC_MAX_ALIGN_T) || defined(__CLANG_MAX_ALIGN_T_DEFINED)
#error resource-free fallback must not fabricate provider flags
#endif
#endif
CHECK(ALIGN(max_align_t) >= ALIGN(void *));
CHECK(ALIGN(max_align_t) >= ALIGN(long long));
CHECK(ALIGN(max_align_t) >= ALIGN(long double));
CHECK(sizeof(max_align_t) % ALIGN(max_align_t) == 0);
struct embedding {
	char lead;
	max_align_t object;
	char tail;
};
CHECK(offsetof(struct embedding, object) == ALIGN(max_align_t));
CHECK(offsetof(struct embedding, tail) == ALIGN(max_align_t) + sizeof(max_align_t));
#ifdef EXPECT_AMD64
#ifndef __x86_64__
#error target mode supports amd64 only
#endif
CHECK(ALIGN(max_align_t) == 16);
CHECK(sizeof(max_align_t) == (EXPECT_PROVIDER ? 32 : 16));
CHECK(offsetof(struct embedding, tail) == (EXPECT_PROVIDER ? 48 : 32));
CHECK(sizeof(struct embedding) == (EXPECT_PROVIDER ? 64 : 48));
#endif
#else
#if defined(_GCC_MAX_ALIGN_T) || defined(__CLANG_MAX_ALIGN_T_DEFINED)
#error C99 must not select the C11 max_align_t provider
#endif
#endif
#if (__STDC_VERSION__ - 0) >= 202311L
#ifndef unreachable
#error C23 unreachable missing
#endif
CHECK(__STDC_VERSION_STDDEF_H__ == 202311L);
static int
c23_path(int yes)
{

	if (yes)
		return 7;
	unreachable();
}
#endif
int
main(void)
{
	const void *null_pointer = NULL;
	if (null_pointer != 0)
		return 1;
#if (__STDC_VERSION__ - 0) >= 202311L
	nullptr_t null_value = nullptr;
	if (null_value != nullptr || c23_path(1) != 7)
		return 2;
#endif
#if (__STDC_VERSION__ - 0) >= 201112L || (__cplusplus - 0) >= 201103L
	max_align_t storage[2];
	unsigned char *bytes = (unsigned char *)(void *)storage;
	bytes[0] = 19;
	bytes[sizeof(storage) - 1] = 23;
	if (bytes[0] != 19 || bytes[sizeof(storage) - 1] != 23)
		return 3;
#endif
	return 0;
}
