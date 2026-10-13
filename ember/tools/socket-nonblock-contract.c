/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted per-call socket nonblocking regression. */
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t expired;

static void
timeout_handler(int signo)
{

	(void)signo;
	expired = 1;
}

static void
require(int condition, const char *message)
{

	if (!condition) {
		perror(message);
		exit(1);
	}
}

static int
check(int call, int flags, int descriptor_nonblock)
{
	char buf[1024] = {0};
	struct iovec iov = { .iov_base = buf, .iov_len = 1 };
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	int s[2], saved, initial, error, failures = 0;
	pid_t reader = -1;
	size_t filled = 0;
	ssize_t n;

	require(socketpair(AF_LOCAL, SOCK_STREAM, 0, s) == 0, "socketpair");
	initial = fcntl(s[0], F_GETFL);
	require(initial != -1, "get flags");
	require(fcntl(s[0], F_SETFL, initial | O_NONBLOCK) == 0, "nonblock");
	while ((n = send(s[0], buf, sizeof(buf), 0)) > 0) {
		filled += (size_t)n;
		require(filled <= 16 * 1024 * 1024, "bounded socket fill");
	}
	require(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK),
	    "fill socket");
	require(fcntl(s[0], F_SETFL,
	    descriptor_nonblock > 0 ? initial | O_NONBLOCK : initial) == 0,
	    "restore flags");
	saved = fcntl(s[0], F_GETFL);
	if (descriptor_nonblock < 0) {
		reader = fork();
		require(reader != -1, "fork reader");
		if (reader == 0) {
			struct timespec delay = { .tv_nsec = 200000000 };
			size_t drained = 0;

			while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
				continue;
			while (drained < filled) {
				n = recv(s[1], buf, sizeof(buf), 0);
				if (n <= 0)
					_exit(1);
				drained += (size_t)n;
			}
			_exit(0);
		}
	}
	expired = 0;
	alarm(1);
	switch (call) {
	case 0:
		n = send(s[0], buf, 1, flags);
		break;
	case 1:
		n = sendto(s[0], buf, 1, flags, NULL, 0);
		break;
	default:
		n = sendmsg(s[0], &msg, flags);
		break;
	}
	error = errno;
	alarm(0);
	if (expired || (reader > 0 ? n != 1 :
	    n != -1 || (error != EAGAIN && error != EWOULDBLOCK)))
		failures++;
	if (reader > 0) {
		int status;

		require(waitpid(reader, &status, 0) == reader, "wait reader");
		require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "reader status");
	}
	if (fcntl(s[0], F_GETFL) != saved)
		failures++;
	printf("%s call=%d flags=%#x descriptor_nonblock=%d filled=%zu "
	    "result=%zd errno=%d alarm=%d\n", failures ? "FAIL" : "PASS",
	    call, flags, descriptor_nonblock, filled, n, error, (int)expired);
	/* Once the peer drains data, the same descriptor remains usable. */
	require(fcntl(s[1], F_SETFL, O_NONBLOCK) == 0, "reader nonblock");
	while ((n = recv(s[1], buf, sizeof(buf), 0)) > 0)
		continue;
	require(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK),
	    "drain socket");
	require(send(s[0], buf, 1, 0) == 1, "send after drain");
	close(s[0]);
	close(s[1]);
	return failures;
}

int
main(void)
{
	struct sigaction sa;
	int failures = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = timeout_handler;
	sigemptyset(&sa.sa_mask);
	require(sigaction(SIGALRM, &sa, NULL) == 0, "sigaction");
	for (int call = 0; call < 3; call++) {
		failures += check(call, MSG_DONTWAIT, 0);
		failures += check(call, MSG_NBIO, 0);
		failures += check(call, 0, 1);
		failures += check(call, 0, -1);
	}
	printf("12 socket cases: %d failures\n", failures);
	return failures != 0;
}
