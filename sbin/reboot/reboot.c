/*	$NetBSD: reboot.c,v 1.41 2019/08/08 21:14:12 roy Exp $	*/

/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1980, 1986, 1993\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)reboot.c	8.1 (Berkeley) 6/5/93";
#else
__RCSID("$NetBSD: reboot.c,v 1.41 2019/08/08 21:14:12 roy Exp $");
#endif
#endif /* not lint */

#include <sys/reboot.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <util.h>
#ifdef SUPPORT_UTMPX
#include <utmpx.h>
#endif

__dead static void usage(void);

static int dohalt;
static int dopoweroff;

enum process_wait_result {
	PROCESS_WAIT_EMPTY,
	PROCESS_WAIT_EXPIRED,
	PROCESS_WAIT_ERROR
};

/* Native broadcast excludes init, the caller, system and zombie processes. */
static enum process_wait_result
wait_for_process_exit(unsigned int seconds)
{
	struct timespec start, now, deadline, remaining, pause;

	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		return PROCESS_WAIT_ERROR;
	if (start.tv_sec < 0 || start.tv_sec > INT64_MAX - seconds ||
	    start.tv_nsec < 0 || start.tv_nsec >= 1000000000) {
		errno = EOVERFLOW;
		return PROCESS_WAIT_ERROR;
	}
	deadline = start;
	deadline.tv_sec += seconds;
	for (;;) {
		/* ESRCH means no permitted broadcast targets, as in native KILL. */
		if (kill(-1, 0) == -1)
			return errno == ESRCH ? PROCESS_WAIT_EMPTY : PROCESS_WAIT_ERROR;
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			return PROCESS_WAIT_ERROR;
		if (now.tv_nsec < 0 || now.tv_nsec >= 1000000000 ||
		    timespeccmp(&now, &start, <)) {
			errno = EIO;
			return PROCESS_WAIT_ERROR;
		}
		if (timespeccmp(&now, &deadline, >=))
			return PROCESS_WAIT_EXPIRED;
		pause.tv_sec = 0;
		pause.tv_nsec = 100000000;
		timespecsub(&deadline, &now, &remaining);
		if (remaining.tv_sec == 0 && remaining.tv_nsec < pause.tv_nsec)
			pause.tv_nsec = remaining.tv_nsec;
		if (nanosleep(&pause, NULL) == -1 && errno != EINTR)
			return PROCESS_WAIT_ERROR;
		/* EINTR never extends the fixed monotonic deadline. */
	}
}

int
main(int argc, char *argv[])
{
	const char *progname;
	int i;
	struct passwd *pw;
	int ch, howto, iflag, lflag, nflag, qflag, sverrno, len;
	enum process_wait_result waited = PROCESS_WAIT_EXPIRED;
	const char *user;
	char *bootstr, **av;

	progname = getprogname();
	if (progname[0] == '-')
		progname++;
	if (strcmp(progname, "halt") == 0) {
		dohalt = 1;
		howto = RB_HALT;
	} else if (strcmp(progname, "poweroff") == 0) {
		dopoweroff = 1;
		howto = RB_HALT | RB_POWERDOWN;
	} else
		howto = 0;
	iflag = lflag = nflag = qflag = 0;
	while ((ch = getopt(argc, argv, "dilnpqvxz")) != -1)
		switch(ch) {
		case 'd':
			howto |= RB_DUMP;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'n':
			nflag = 1;
			howto |= RB_NOSYNC;
			break;
		case 'p':
			if (dohalt == 0)
				usage();
			howto |= RB_POWERDOWN;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'v':
			howto |= AB_VERBOSE;
			break;
		case 'x':
			howto |= AB_DEBUG;
			break;
		case 'z':
			howto |= AB_SILENT;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (iflag && (nflag || qflag)) {
		warnx("-i is incompatible with -n and -q");
		usage();
	}

	if (argc) {
		for (av = argv, len = 0; *av; av++)
			len += strlen(*av) + 1;
		bootstr = malloc(len + 1);
		if (bootstr == NULL)
			err(1, "malloc");
		*bootstr = '\0';		/* for first strcat */
		for (av = argv; *av; av++) {
			strcat(bootstr, *av);
			strcat(bootstr, " ");
		}
		bootstr[len - 1] = '\0';	/* to kill last space */
		howto |= RB_STRING;
	} else
		bootstr = NULL;

	if (geteuid())
		errx(1, "%s", strerror(EPERM));

	if (qflag) {
		reboot(howto, bootstr);
		err(1, "reboot");
	}

	/* Log the reboot. */
	if (!lflag)  {
		if ((user = getlogin()) == NULL)
			user = (pw = getpwuid(getuid())) ?
			    pw->pw_name : "???";
		if (dohalt) {
			openlog("halt", LOG_CONS, LOG_AUTH);
			syslog(LOG_CRIT, "halted by %s", user);
		} else if (dopoweroff) {
			openlog("poweroff", LOG_CONS, LOG_AUTH);
			syslog(LOG_CRIT, "powered off by %s", user);
		} else {
			openlog("reboot", LOG_CONS, LOG_AUTH);
			if (bootstr)
				syslog(LOG_CRIT, "rebooted by %s: %s", user,
				    bootstr);
			else
				syslog(LOG_CRIT, "rebooted by %s", user);
		}
	}
#ifdef SUPPORT_UTMP
	logwtmp("~", "shutdown", "");
#endif
#ifdef SUPPORT_UTMPX
	logwtmpx("~", "shutdown", "", 0, INIT_PROCESS);
#endif

	/*
	 * Do a sync early on, so disks start transfers while we're off
	 * killing processes.  Don't worry about writes done before the
	 * processes die, the reboot system call syncs the disks.
	 */
	if (!nflag)
		sync();

	/* 
	 * Ignore signals that we can get as a result of killing
	 * parents, group leaders, etc.
	 */
	(void)signal(SIGHUP,  SIG_IGN);
	(void)signal(SIGINT,  SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTERM, SIG_IGN);
	(void)signal(SIGTSTP, SIG_IGN);

	/*
	 * If we're running in a pipeline, we don't want to die
	 * after killing whatever we're writing to.
	 */
	(void)signal(SIGPIPE, SIG_IGN);

	/* Just stop init -- if we fail, we'll restart it. */
	if (kill(1, SIGTSTP) == -1)
		err(1, "SIGTSTP init");

	/* Send a SIGTERM first, a chance to save the buffers. */
	if (kill(-1, SIGTERM) == -1) {
		/*
		 * If ESRCH, everything's OK: we're the only non-system
		 * process!  That can happen e.g. via 'exec reboot' in
		 * single-user mode.
		 */
		if (errno != ESRCH) {
			warn("SIGTERM all processes");
			goto restart;
		}
	}

	/*
	 * After the processes receive the signal, start the rest of the
	 * buffers on their way.  Wait 5 seconds between the SIGTERM and
	 * the SIGKILL to pretend to give everybody a chance.
	 */
	if (iflag) {
		waited = wait_for_process_exit(2);
		if (waited == PROCESS_WAIT_ERROR) {
			warn("waiting for processes");
			goto restart;
		}
	} else
		sleep(2);
	if (!nflag)
		sync();
	if (iflag) {
		if (waited != PROCESS_WAIT_EMPTY &&
		    wait_for_process_exit(3) == PROCESS_WAIT_ERROR) {
			warn("waiting for processes");
			goto restart;
		}
	} else
		sleep(3);

	for (i = 1;; ++i) {
		if (kill(-1, SIGKILL) == -1) {
			if (errno == ESRCH)
				break;
			warn("SIGKILL all processes");
			goto restart;
		}
		if (i > 5) {
			warnx("WARNING: some process(es) wouldn't die");
			break;
		}
		if (iflag) {
			waited = wait_for_process_exit(2 * i);
			if (waited == PROCESS_WAIT_ERROR) {
				warn("waiting for processes");
				goto restart;
			}
			/* Even an empty poll must reach the next native KILL check. */
		} else
			(void)sleep(2 * i);
	}

	reboot(howto, bootstr);
	warn("reboot()");
	/* FALLTHROUGH */

restart:
	sverrno = errno;
	errx(1, "%s%s", kill(1, SIGHUP) == -1 ? "(can't restart init): " : "",
	    strerror(sverrno));
	/* NOTREACHED */
}

static void
usage(void)
{
	const char *pflag = dohalt ? "p" : "";

	(void)fprintf(stderr, "usage: %s [-diln%sqvxz] [-- <boot string>]\n",
	    getprogname(), pflag);
	exit(1);
}
