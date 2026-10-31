/* SPDX-License-Identifier: BSD-2-Clause */
/* Match upstream CMake's BIG_ENDIAN selection for the target compiler. */
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_BIG_ENDIAN__)
#error "target compiler does not define byte order"
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define IS_BIG_ENDIAN
#endif
