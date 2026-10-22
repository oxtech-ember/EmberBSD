/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted tipout signal regression. */
#include <sys/wait.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SCRIPT_INDEX 17
#define BEAUTIFY_INDEX 0

typedef struct {
	char *v_value;
} value_t;

FILE *fscript;
int attndes[2], fildes[2], repdes[2], FD, bits8;
char ccc;
value_t vtable[40];

int
any(char ch, const char *set)
{

	(void)ch;
	(void)set;
	return 0;
}

void tipout(void);

enum stop_case { CASE_INPUT, CASE_WRITE, CASE_CARRIER };

static enum stop_case stop_case;
static int under_test;
static int sync_wr;
static char script_path[1024];
static volatile sig_atomic_t in_handler;
static volatile sig_atomic_t script_closed;
static void (*user_handler)(int);

static ssize_t (*real_read)(int, void *, size_t);
static size_t (*real_fwrite)(const void *, size_t, size_t, FILE *);
static int (*real_fclose)(FILE *);
static void (*real_exit)(int);
static sig_t (*real_signal)(int, sig_t);
static int (*real_sigaction)(int, const struct sigaction *, struct sigaction *);

static void
init_reals(void)
{

	real_read = dlsym(RTLD_NEXT, "read");
	real_fwrite = dlsym(RTLD_NEXT, "fwrite");
	real_fclose = dlsym(RTLD_NEXT, "fclose");
	real_exit = dlsym(RTLD_NEXT, "exit");
	real_signal = dlsym(RTLD_NEXT, "signal");
	real_sigaction = dlsym(RTLD_NEXT, "sigaction");
	if (real_read == NULL || real_fwrite == NULL || real_fclose == NULL ||
	    real_exit == NULL || real_signal == NULL || real_sigaction == NULL) {
		fprintf(stderr, "missing libc symbols\n");
		_exit(1);
	}
}

static void
watch(int signo)
{

	in_handler = 1;
	user_handler(signo);
	in_handler = 0;
}

sig_t
signal(int signo, sig_t handler)
{

	if (!under_test || handler == SIG_IGN || handler == SIG_DFL)
		return real_signal(signo, handler);
	user_handler = handler;
	return real_signal(signo, watch);
}

int
sigaction(int signo, const struct sigaction *act, struct sigaction *old)
{
	struct sigaction wrapped;

	if (!under_test || act == NULL || act->sa_handler == SIG_IGN ||
	    act->sa_handler == SIG_DFL)
		return real_sigaction(signo, act, old);
	user_handler = act->sa_handler;
	wrapped = *act;
	wrapped.sa_handler = watch;
	return real_sigaction(signo, &wrapped, old);
}

static ssize_t
wait_interrupt(int signo, char mark)
{
	sigset_t block, old, waitmask;

	sigemptyset(&block);
	sigaddset(&block, signo);
	if (sigprocmask(SIG_BLOCK, &block, &old) != 0)
		_exit(47);
	if (write(sync_wr, &mark, 1) != 1)
		_exit(46);
	waitmask = old;
	(void)sigdelset(&waitmask, signo);
	if (sigsuspend(&waitmask) != -1)
		_exit(47);
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0)
		_exit(47);
	errno = EINTR;
	return -1;
}

ssize_t
read(int fd, void *buf, size_t n)
{

	if (under_test && stop_case == CASE_INPUT && fd == FD)
		return wait_interrupt(SIGTERM, 'I');
	return real_read(fd, buf, n);
}

size_t
fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp)
{
	size_t wrote;

	if (!(under_test && stop_case == CASE_WRITE))
		return real_fwrite(ptr, size, nmemb, fp);
	wrote = real_fwrite(ptr, size, nmemb, fp);
	if (wrote != nmemb)
		_exit(48);
	(void)wait_interrupt(SIGHUP, 'W');
	return wrote;
}

int
fclose(FILE *fp)
{

	if (under_test && in_handler)
		_exit(42);
	if (under_test && script_closed)
		_exit(44);
	if (real_fclose(fp) != 0)
		return EOF;
	if (under_test)
		script_closed = 1;
	return 0;
}

void
exit(int status)
{

	if (!under_test)
		real_exit(status);
	if (in_handler)
		_exit(42);
	if (!script_closed)
		_exit(43);
	_exit(status == 0 ? 0 : status);
}

static int
wait_byte(int fd, char expect)
{
	struct pollfd pfd;
	char got;

	pfd.fd = fd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 2000) != 1 || (pfd.revents & POLLIN) == 0)
		return -1;
	if (read(fd, &got, 1) != 1 || got != expect)
		return -1;
	return 0;
}

static int
reap(pid_t pid, int *status)
{
	int i;

	for (i = 0; i < 40; i++) {
		if (waitpid(pid, status, WNOHANG) == pid)
			return 0;
		(void)poll(NULL, 0, 50);
	}
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, status, 0);
	return -1;
}

static void
stop_child(pid_t pid)
{
	int status;

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);
}

static int
run_case(const char *name, enum stop_case which, int signo, const char *payload)
{
	int remote[2], syncp[2], scriptfd;
	pid_t pid;
	int status;
	char stored[64];
	ssize_t nstored;

	if (pipe(remote) != 0 || pipe(syncp) != 0) {
		fprintf(stderr, "%s: pipe failed\n", name);
		return 1;
	}
	scriptfd = mkstemp(script_path);
	if (scriptfd < 0) {
		fprintf(stderr, "%s: script file failed\n", name);
		return 1;
	}
	(void)close(scriptfd);
	stop_case = which;
	sync_wr = syncp[1];
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "%s: fork failed\n", name);
		return 1;
	}
	if (pid == 0) {
		(void)close(remote[1]);
		(void)close(syncp[0]);
		FD = remote[0];
		if (pipe(attndes) != 0)
			_exit(50);
		fscript = fopen(script_path, "a");
		if (fscript == NULL)
			_exit(50);
		vtable[SCRIPT_INDEX].v_value = (char *)(intptr_t)1;
		vtable[BEAUTIFY_INDEX].v_value = (char *)(intptr_t)0;
		script_closed = 0;
		under_test = 1;
		if (write(sync_wr, "R", 1) != 1)
			_exit(51);
		if (dup2(open("/dev/null", O_WRONLY), STDOUT_FILENO) < 0)
			_exit(52);
		tipout();
		_exit(49);
	}
	(void)close(remote[0]);
	(void)close(syncp[1]);
	if (wait_byte(syncp[0], 'R') != 0) {
		fprintf(stderr, "%s: child did not reach tipout\n", name);
		stop_child(pid);
		return 1;
	}
	if (which == CASE_CARRIER)
		(void)close(remote[1]);
	else {
		if (write(remote[1], payload, strlen(payload)) != (ssize_t)strlen(payload)) {
			fprintf(stderr, "%s: remote write failed\n", name);
			stop_child(pid);
			return 1;
		}
		if (wait_byte(syncp[0], which == CASE_INPUT ? 'I' : 'W') != 0) {
			fprintf(stderr, "%s: child did not block in the selected operation\n", name);
			stop_child(pid);
			return 1;
		}
		if (kill(pid, signo) != 0) {
			fprintf(stderr, "%s: kill failed\n", name);
			stop_child(pid);
			return 1;
		}
	}
	if (reap(pid, &status) != 0) {
		fprintf(stderr, "%s: hung after the signal\n", name);
		return 1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "%s: exit status %d\n", name,
		    WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
		return 1;
	}
	scriptfd = open(script_path, O_RDONLY);
	if (scriptfd < 0) {
		fprintf(stderr, "%s: script file disappeared\n", name);
		return 1;
	}
	nstored = read(scriptfd, stored, sizeof(stored));
	(void)close(scriptfd);
	(void)unlink(script_path);
	if (which == CASE_WRITE) {
		if (nstored != (ssize_t)strlen(payload) ||
		    memcmp(stored, payload, strlen(payload)) != 0) {
			fprintf(stderr, "%s: script bytes were not kept\n", name);
			return 1;
		}
	} else if (nstored != 0) {
		fprintf(stderr, "%s: unexpected script bytes\n", name);
		return 1;
	}
	if (which != CASE_CARRIER)
		(void)close(remote[1]);
	(void)close(syncp[0]);
	printf("PASS: %s\n", name);
	return 0;
}

int
main(void)
{
	char path[] = "/tmp/tip-stop-script-XXXXXX";

	init_reals();
	memcpy(script_path, path, sizeof(path));
	if (run_case("input", CASE_INPUT, SIGTERM, "x") != 0)
		return 1;
	memcpy(script_path, path, sizeof(path));
	if (run_case("script-write", CASE_WRITE, SIGHUP, "Hello\n") != 0)
		return 1;
	memcpy(script_path, path, sizeof(path));
	if (run_case("carrier-loss", CASE_CARRIER, 0, "") != 0)
		return 1;
	return 0;
}
