/* Target feature contract for the Ports libedit source on EmberBSD. */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/cdefs.h>
#include <stdint.h>
#include <wchar.h>

#define HAVE_GETLINE 1
#define HAVE_REALLOCARR 1
#define HAVE_SIZE_MAX 1
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1
#define HAVE_STRUNVIS 1
#define HAVE_STRVIS 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_U_INT32_T 1
#define HAVE_VIS 1
#define HAVE_WCSDUP 1

/* Define to 1 if you have the <curses.h> header file. */
#define HAVE_CURSES_H 1

/* Define to 1 if you have getpwnam_r and getpwuid_r that are draft POSIX.1
   versions. */
/* #undef HAVE_GETPW_R_DRAFT */

/* Define to 1 if you have getpwnam_r and getpwuid_r that are POSIX.1
   compatible. */
#define HAVE_GETPW_R_POSIX 1

/* Define to 1 if you have the `issetugid' function. */
#define HAVE_ISSETUGID 1

/* Define to 1 if you have the <ncurses.h> header file. */
/* #undef HAVE_NCURSES_H */

/* Define to 1 if struct dirent has member d_namlen */
#define HAVE_STRUCT_DIRENT_D_NAMLEN 1

/* Define to 1 if you have the <sys/cdefs.h> header file. */
#define HAVE_SYS_CDEFS_H 1

/* Define to 1 if you have the <termcap.h> header file. */
#define HAVE_TERMCAP_H 1

/* Define to 1 if you have the <term.h> header file. */
#define HAVE_TERM_H 1

#include "sys.h"

