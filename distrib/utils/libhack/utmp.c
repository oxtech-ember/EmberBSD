/*	$NetBSD: utmp.c,v 1.5 2003/07/26 17:07:37 salo Exp $	*/

/*
 * Written by Gordon W. Ross <gwr@NetBSD.org>
 * Public domain.
 */

/* Simplified (do nothing:) */
#include <sys/types.h>
#include <utmp.h>
#include <util.h>

void
login(const struct utmp *ut)
{
}

void
loginx(const struct utmpx *ut)
{
}

int
logout(const char *line)
{
	return(0);
}

int
logoutx(const char *line, int status, int type)
{
	return(0);
}

void
logwtmp(const char *line, const char *name, const char *host)
{
}

void
logwtmpx(const char *line, const char *name, const char *host, int status,
    int type)
{
}
