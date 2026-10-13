/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted LOCAL_PEEREID socketpair regression. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int cases, failures, skips;
static volatile sig_atomic_t active_child;

static void
timeout_handler(int signo)
{
	static const char message[] = "FAIL: peer credential test timed out\n";

	(void)signo;
	if (active_child > 0)
		kill(active_child, SIGKILL);
	(void)write(STDERR_FILENO, message, sizeof(message) - 1);
	_exit(124);
}

static void
require(int condition, const char *message)
{

	if (!condition) {
		perror(message);
		exit(2);
	}
}

static struct unpcbid
current_identity(void)
{
	const struct unpcbid id = {
		.unp_pid = getpid(),
		.unp_euid = geteuid(),
		.unp_egid = getegid(),
	};

	return id;
}

static void
check_identity(int fd, const struct unpcbid *expected, int type,
    const char *label)
{
	struct unpcbid actual;
	socklen_t size = sizeof(actual);
	int error, result, good;

	memset(&actual, 0, sizeof(actual));
	errno = 0;
	result = getsockopt(fd, SOL_LOCAL, LOCAL_PEEREID, &actual, &size);
	error = errno;
	good = result == 0 && size == sizeof(actual) &&
	    actual.unp_pid == expected->unp_pid &&
	    actual.unp_euid == expected->unp_euid &&
	    actual.unp_egid == expected->unp_egid;
	printf("%s type=%d %s result=%d errno=%d "
	    "peer=%ld/%lu/%lu expected=%ld/%lu/%lu\n", good ? "PASS" : "FAIL",
	    type, label, result, error, (long)actual.unp_pid,
	    (unsigned long)actual.unp_euid, (unsigned long)actual.unp_egid,
	    (long)expected->unp_pid, (unsigned long)expected->unp_euid,
	    (unsigned long)expected->unp_egid);
	cases++;
	failures += !good;
}

static void
check_rejected(int fd, int type, const char *label)
{
	struct unpcbid actual;
	socklen_t size = sizeof(actual);
	int result, error;

	errno = 0;
	result = getsockopt(fd, SOL_LOCAL, LOCAL_PEEREID, &actual, &size);
	error = errno;
	printf("%s type=%d %s result=%d errno=%d\n",
	    result == -1 && error == EINVAL ? "PASS" : "FAIL",
	    type, label, result, error);
	cases++;
	failures += result != -1 || error != EINVAL;
}

static void
wait_child(pid_t child, unsigned int child_cases)
{
	int status;
	pid_t result;

	active_child = child;
	do {
		result = waitpid(child, &status, 0);
	} while (result == -1 && errno == EINTR);
	active_child = 0;
	require(result == child, "waitpid");
	cases += child_cases;
	if (WIFEXITED(status) && WEXITSTATUS(status) <= child_cases) {
		failures += WEXITSTATUS(status);
	} else {
		printf("FAIL child status=%#x\n", status);
		failures++;
	}
}

static void
check_inherited(int type, int changed_credentials)
{
	struct unpcbid creator = current_identity();
	int pair[2];
	pid_t child;

	if (changed_credentials && geteuid() != 0) {
		printf("SKIP type=%d changed credentials require root (4 cases)\n",
		    type);
		skips += 4;
		return;
	}
	require(socketpair(AF_LOCAL, type, 0, pair) == 0, "socketpair");
	child = fork();
	require(child != -1, "fork");
	if (child == 0) {
		struct unpcbid changed;
		int fresh[2];

		active_child = 0;
		failures = cases = 0;
		alarm(5);
		if (changed_credentials) {
			require(setgid(65534) == 0, "setgid child");
			require(setuid(65534) == 0, "setuid child");
		}
		check_identity(pair[0], &creator, type,
		    changed_credentials ? "changed-caller end0" : "fork end0");
		check_identity(pair[1], &creator, type,
		    changed_credentials ? "changed-caller end1" : "fork end1");
		if (changed_credentials) {
			changed = current_identity();
			require(socketpair(AF_LOCAL, type, 0, fresh) == 0,
			    "socketpair changed child");
			check_identity(fresh[0], &changed, type, "new-creator end0");
			check_identity(fresh[1], &changed, type, "new-creator end1");
			close(fresh[0]);
			close(fresh[1]);
		}
		close(pair[0]);
		close(pair[1]);
		_exit(failures);
	}
	close(pair[0]);
	close(pair[1]);
	wait_child(child, changed_credentials ? 4 : 2);
}

static void
check_named(int type)
{
	struct unpcbid server = current_identity(), client;
	struct sockaddr_un address;
	char directory[] = "/tmp/socket-peercred.XXXXXX";
	int listener, accepted;
	pid_t child;

	require(mkdtemp(directory) != NULL, "mkdtemp");
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_LOCAL;
	require(snprintf(address.sun_path, sizeof(address.sun_path), "%s/s",
	    directory) < (int)sizeof(address.sun_path), "socket path");
	listener = socket(AF_LOCAL, type, 0);
	require(listener != -1, "listener socket");
	require(bind(listener, (struct sockaddr *)&address,
	    SUN_LEN(&address)) == 0, "bind");
	require(listen(listener, 1) == 0, "listen");
	child = fork();
	require(child != -1, "fork connector");
	if (child == 0) {
		int fd;

		active_child = 0;
		failures = cases = 0;
		alarm(5);
		close(listener);
		fd = socket(AF_LOCAL, type, 0);
		require(fd != -1, "client socket");
		require(connect(fd, (struct sockaddr *)&address,
		    SUN_LEN(&address)) == 0, "connect");
		check_identity(fd, &server, type, "named client sees server");
		close(fd);
		_exit(failures);
	}
	active_child = child;
	accepted = accept(listener, NULL, NULL);
	require(accepted != -1, "accept");
	client = server;
	client.unp_pid = child;
	check_identity(accepted, &client, type, "named server sees client");
	close(accepted);
	close(listener);
	wait_child(child, 1);
	require(unlink(address.sun_path) == 0, "unlink socket");
	require(rmdir(directory) == 0, "rmdir");
}

int
main(void)
{
	const int types[] = { SOCK_STREAM, SOCK_SEQPACKET };
	struct sigaction action;
	struct unpcbid creator = current_identity();
	int pair[2], fd;

	setvbuf(stdout, NULL, _IONBF, 0);
	memset(&action, 0, sizeof(action));
	action.sa_handler = timeout_handler;
	sigemptyset(&action.sa_mask);
	require(sigaction(SIGALRM, &action, NULL) == 0, "sigaction");
	alarm(15);
	for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		const int type = types[i];

		require(socketpair(AF_LOCAL, type, 0, pair) == 0, "socketpair");
		check_identity(pair[0], &creator, type, "creator end0");
		check_identity(pair[1], &creator, type, "creator end1");
		close(pair[0]);
		close(pair[1]);
		check_inherited(type, 0);
		check_inherited(type, 1);
		fd = socket(AF_LOCAL, type, 0);
		require(fd != -1, "unconnected socket");
		check_rejected(fd, type, "unconnected");
		close(fd);
		check_named(type);
	}
	/* LOCAL_PEEREID is not defined for datagrams by unix(4). */
	require(socketpair(AF_LOCAL, SOCK_DGRAM, 0, pair) == 0,
	    "datagram socketpair");
	check_rejected(pair[0], SOCK_DGRAM, "datagram end0");
	check_rejected(pair[1], SOCK_DGRAM, "datagram end1");
	close(pair[0]);
	close(pair[1]);
	fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
	require(fd != -1, "unconnected datagram");
	check_rejected(fd, SOCK_DGRAM, "unconnected datagram");
	close(fd);
	alarm(0);
	printf("%u peer credential cases: %u failures, %u skipped\n",
	    cases, failures, skips);
	return failures != 0;
}
