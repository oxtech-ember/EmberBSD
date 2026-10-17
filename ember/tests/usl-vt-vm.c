/* SPDX-License-Identifier: BSD-2-Clause */
/* Explicitly provisioned, idle QEMU wscons fixture only. No device backend. */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int
set_signal(int number, void (*handler)(int))
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	if (sigemptyset(&action.sa_mask) == -1)
		return (-1);
	return (sigaction(number, &action, NULL));
}

static int64_t
now_ms(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) == -1)
		abort();
	return ((int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static int
decimal(const char *s, unsigned long max, unsigned long *out)
{
	unsigned long n = 0, d;
	if (s == NULL || *s == '\0' || (s[0] == '0' && s[1] != '\0'))
		return (-1);
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return (-1);
		d = (unsigned long)(*s - '0');
		if (d > max || n > (max - d) / 10)
			return (-1);
		n = n * 10 + d;
	}
	*out = n;
	return (0);
}

static int
screen_number(const char *s, int *vt)
{
	unsigned long n;
	/* VT_GETSTATE exposes a 16-bit mask, with bit 0 unused. */
	if (strncmp(s, "/dev/ttyE", 9) || decimal(s + 9, 14, &n))
		return (-1);
	*vt = (int)n + 1;
	return (0);
}

struct options {
	const char *fixture, *screen, *back, *uidstr;
	int vt, original;
	unsigned long uid;
};

static int
parse_options(int argc, char **argv, struct options *o)
{
	int explicit_vm = 0, i;
	memset(o, 0, sizeof(*o));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--vm-fixture") && !explicit_vm)
			explicit_vm = 1;
		else if (i + 1 < argc && !strcmp(argv[i], "--fixture-record") &&
			 o->fixture == NULL)
			o->fixture = argv[++i];
		else if (i + 1 < argc && !strcmp(argv[i], "--screen") &&
			 o->screen == NULL)
			o->screen = argv[++i];
		else if (i + 1 < argc && !strcmp(argv[i], "--return-screen") &&
			 o->back == NULL)
			o->back = argv[++i];
		else if (i + 1 < argc && !strcmp(argv[i], "--uid") &&
			 o->uidstr == NULL)
			o->uidstr = argv[++i];
		else
			return (-1);
	}
	return (!explicit_vm || o->fixture == NULL || o->fixture[0] != '/' ||
		       o->screen == NULL || o->back == NULL || o->uidstr == NULL ||
		       screen_number(o->screen, &o->vt) ||
		       screen_number(o->back, &o->original) ||
		       o->vt == o->original ||
		       decimal(o->uidstr, UINT_MAX, &o->uid) || o->uid == 0
		   ? -1
		   : 0);
}

static int
fixture_stat(const struct stat *st)
{
	return (S_ISREG(st->st_mode) && (st->st_mode & 07777) == 0600 &&
		       st->st_uid == 0 && st->st_nlink == 1
		   ? 0
		   : -1);
}

static int
ready(int fd, short events, int64_t deadline)
{
	struct pollfd p = {.fd = fd, .events = events};
	int r;
	for (;;) {
		int64_t left = deadline - now_ms();
		if (left <= 0) {
			errno = ETIMEDOUT;
			return (-1);
		}
		r = poll(&p, 1, left > INT_MAX ? INT_MAX : (int)left);
		if (r == -1 && errno == EINTR)
			continue;
		if (r > 0 && (p.revents & events))
			return (0);
		if (r == 0)
			errno = ETIMEDOUT;
		else if (r > 0)
			errno = EPIPE;
		return (-1);
	}
}

static int
pass_fd(int socket, int fd)
{
	union {
		struct cmsghdr align;
		unsigned char b[CMSG_SPACE(sizeof(int))];
	} u;
	char byte = 'F';
	struct iovec iov = {&byte, 1};
	struct msghdr msg;
	struct cmsghdr *c;
	memset(&msg, 0, sizeof(msg));
	memset(&u, 0, sizeof(u));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.b;
	msg.msg_controllen = sizeof(u.b);
	c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(fd));
	memcpy(CMSG_DATA(c), &fd, sizeof(fd));
	if (ready(socket, POLLOUT, now_ms() + 1000))
		return (-1);
	return (sendmsg(socket, &msg, 0) == 1 ? 0 : -1);
}

static int
take_fd(int socket)
{
	union {
		struct cmsghdr align;
		unsigned char b[CMSG_SPACE(16 * sizeof(int))];
	} u;
	char byte = 0;
	int fd = -1, count = 0;
	struct iovec iov = {&byte, 1};
	struct msghdr msg;
	struct cmsghdr *c;
	memset(&msg, 0, sizeof(msg));
	memset(&u, 0, sizeof(u));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.b;
	msg.msg_controllen = sizeof(u.b);
	if (ready(socket, POLLIN, now_ms() + 1000))
		return (-1);
	if (recvmsg(socket, &msg, 0) != 1)
		return (-1);
	for (c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
		    c->cmsg_len >= CMSG_LEN(sizeof(int))) {
			size_t bytes = c->cmsg_len - CMSG_LEN(0), offset;
			size_t available =
			    (size_t)((char *)msg.msg_control +
				     msg.msg_controllen - (char *)CMSG_DATA(c));
			if (bytes > available)
				bytes = available;
			for (offset = 0; offset + sizeof(int) <= bytes;
			    offset += sizeof(int)) {
				int next;
				memcpy(&next, (char *)CMSG_DATA(c) + offset,
				    sizeof(next));
				if (fd >= 0)
					close(fd);
				fd = next;
				count++;
			}
		}
	}
	if (byte != 'F' || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
	    count != 1) {
		if (fd >= 0)
			close(fd);
		errno = EINVAL;
		return (-1);
	}
	return (fd);
}

static int
reap(pid_t pid, int milliseconds, int *status)
{
	int64_t deadline = now_ms() + milliseconds;
	struct timespec delay = {0, 1000000};
	for (;;) {
		pid_t r = waitpid(pid, status, WNOHANG);
		if (r == pid)
			return (0);
		if (r == -1 && errno != EINTR)
			return (-1);
		if (now_ms() >= deadline) {
			errno = ETIMEDOUT;
			return (-1);
		}
		(void)nanosleep(&delay, NULL);
	}
}

static int
stop_child(pid_t pid)
{
	int status;
	if (pid <= 0)
		return (0);
	if (reap(pid, 20, &status) == 0 || errno == ECHILD)
		return (0);
	if (kill(pid, SIGTERM) == -1 && errno != ESRCH)
		return (-1);
	if (reap(pid, 200, &status) == 0 || errno == ECHILD)
		return (0);
	if (kill(pid, SIGKILL) == -1 && errno != ESRCH)
		return (-1);
	return (reap(pid, 1000, &status));
}

#ifndef USL_VM_HOST_TEST
#ifndef __NetBSD__
#error Build this runner for EmberBSD with the audited target toolchain
#endif
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#define COMMAND_MS 2000
static volatile sig_atomic_t interrupted;
static void
on_signal(int n)
{
	interrupted = n;
}
struct packet {
	int op, arg, result, error;
};
struct actor {
	pid_t pid;
	int socket;
};
enum { PROCESS = 1, AUTO, ACK, WAIT_SIGNAL, CHILD_ACK, EXIT_ACTOR, READ_MODE };

static void
record(const char *what, int error)
{
	printf("%lld %s errno=%d\n", (long long)now_ms(), what, error);
	fflush(stdout);
}

static int
transfer(int fd, void *buffer, size_t size, int writing)
{
	char *p = buffer;
	int64_t deadline = now_ms() + COMMAND_MS;
	while (size) {
		ssize_t n;
		if (interrupted) {
			errno = EINTR;
			return (-1);
		}
		if (ready(fd, writing ? POLLOUT : POLLIN, deadline))
			return (-1);
		n = writing ? write(fd, p, size) : read(fd, p, size);
		if (n == -1 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (n <= 0) {
			if (!n)
				errno = EPIPE;
			return (-1);
		}
		p += n;
		size -= (size_t)n;
	}
	return (0);
}

static int
command(const struct actor *a, int op, int arg, int expected)
{
	struct packet p = {.op = op, .arg = arg};
	if (transfer(a->socket, &p, sizeof(p), 1) ||
	    transfer(a->socket, &p, sizeof(p), 0))
		return (-1);
	if (p.op != op || p.arg != arg || p.error != expected ||
	    (expected == 0 ? p.result < 0 : p.result != -1)) {
		record("unexpected-actor-response", p.error);
		errno = EPROTO;
		return (-1);
	}
	return (0);
}

static int
fork_ack(int fd, int arg)
{
	int sockets[2], result, status;
	pid_t child;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
		return (-1);
	child = fork();
	if (child == 0) {
		close(sockets[0]);
		result = ioctl(fd, VT_RELDISP, arg) == -1 ? errno : 0;
		(void)write(sockets[1], &result, sizeof(result));
		_exit(0);
	}
	close(sockets[1]);
	if (child < 0) {
		close(sockets[0]);
		return (-1);
	}
	if (transfer(sockets[0], &result, sizeof(result), 0) ||
	    reap(child, COMMAND_MS, &status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status)) {
		close(sockets[0]);
		(void)stop_child(child);
		errno = EIO;
		return (-1);
	}
	close(sockets[0]);
	if (result) {
		errno = result;
		return (-1);
	}
	return (0);
}

static void
actor_loop(int socket, uid_t uid, gid_t gid)
{
	int fd;
	sigset_t signals;
	struct packet p;
	struct vt_mode mode = {
	    .mode = VT_PROCESS, .relsig = SIGUSR1, .acqsig = SIGUSR2};
	if (set_signal(SIGTERM, SIG_DFL) || set_signal(SIGINT, SIG_DFL) ||
	    set_signal(SIGHUP, SIG_DFL))
		_exit(111);
	sigemptyset(&signals);
	sigaddset(&signals, SIGUSR1);
	sigaddset(&signals, SIGUSR2);
	if (sigprocmask(SIG_BLOCK, &signals, NULL) ||
	    (fd = take_fd(socket)) < 0 || setgroups(0, NULL) || setgid(gid) ||
	    setuid(uid) || getuid() != uid || geteuid() != uid)
		_exit(110);
	while (transfer(socket, &p, sizeof(p), 0) == 0) {
		errno = 0;
		switch (p.op) {
		case PROCESS:
			p.result = ioctl(fd, VT_SETMODE, &mode);
			break;
		case AUTO: {
			struct vt_mode automatic = {.mode = VT_AUTO};
			p.result = ioctl(fd, VT_SETMODE, &automatic);
			break;
		}
		case ACK:
			p.result = ioctl(fd, VT_RELDISP, p.arg);
			break;
		case CHILD_ACK:
			p.result = fork_ack(fd, p.arg);
			break;
		case READ_MODE: {
			struct vt_mode current;
			memset(&current, 0, sizeof(current));
			p.result = ioctl(fd, VT_GETMODE, &current);
			if (p.result == 0 && current.mode != VT_PROCESS) {
				p.result = -1;
				errno = EPROTO;
			}
			break;
		}
		case WAIT_SIGNAL: {
			struct timespec limit = {1, 0};
			int n = sigtimedwait(&signals, NULL, &limit);
			if (n == p.arg)
				p.result = 0;
			else {
				p.result = -1;
				if (n >= 0)
					errno = EPROTO;
			}
			break;
		}
		case EXIT_ACTOR:
			_exit(0);
		default:
			p.result = -1;
			errno = EINVAL;
			break;
		}
		p.error = p.result < 0 ? errno : 0;
		if (transfer(socket, &p, sizeof(p), 1))
			break;
	}
	close(fd);
	close(socket);
	_exit(0);
}

static int
spawn_actor(struct actor *a, int screen, int other, int control,
    int prior_socket, uid_t uid, gid_t gid)
{
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
		return (-1);
	a->pid = fork();
	if (a->pid == 0) {
		close(sockets[0]);
		close(screen);
		close(other);
		close(control);
		if (prior_socket >= 0)
			close(prior_socket);
		actor_loop(sockets[1], uid, gid);
	}
	close(sockets[1]);
	if (a->pid < 0) {
		close(sockets[0]);
		return (-1);
	}
	a->socket = sockets[0];
	return (pass_fd(a->socket, screen));
}

static int
get_active(int fd)
{
	int active;
	return (ioctl(fd, VT_GETACTIVE, &active) == -1 ? -1 : active);
}

static int
await_active(int fd, int expected)
{
	int64_t deadline = now_ms() + COMMAND_MS;
	struct timespec delay = {0, 1000000};
	do {
		int active = get_active(fd);
		if (active == expected)
			return (0);
		if (active < 0 || interrupted)
			return (-1);
		(void)nanosleep(&delay, NULL);
	} while (now_ms() < deadline);
	errno = ETIMEDOUT;
	return (-1);
}

static int
own_mode(int fd)
{
	struct vt_mode mode;
	memset(&mode, 0, sizeof(mode));
	if (ioctl(fd, VT_GETMODE, &mode))
		return (-1);
	if (mode.mode == VT_AUTO)
		return (0);
	if (mode.mode == VT_PROCESS && mode.relsig == SIGUSR1 &&
	    mode.acqsig == SIGUSR2)
		return (1);
	errno = EPROTO;
	return (-1);
}

static int
restore_fixture(
    int control, int fd, int backfd, int vt, int original)
{
	struct vt_mode automatic = {.mode = VT_AUTO};
	unsigned int display_mode;
	int current = own_mode(fd), active;
	if (current < 0 || own_mode(backfd) != 0)
		return (-1);
	if (current == 1 && ioctl(fd, VT_SETMODE, &automatic))
		return (-1);
	active = get_active(control);
	if (active != original &&
	    (active != vt || ioctl(control, VT_ACTIVATE, original) ||
		await_active(control, original)))
		return (-1);
	if (own_mode(fd) != 0 || own_mode(backfd) != 0 ||
	    ioctl(fd, WSDISPLAYIO_GMODE, &display_mode) ||
	    display_mode != WSDISPLAYIO_MODE_EMUL ||
	    ioctl(backfd, WSDISPLAYIO_GMODE, &display_mode) ||
	    display_mode != WSDISPLAYIO_MODE_EMUL)
		return (-1);
	return (0);
}

static int
screen_fd(const char *path, devmajor_t major_number, int vt)
{
	struct stat st;
	int fd = open(path, O_RDWR | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return (-1);
	if (fstat(fd, &st) || !S_ISCHR(st.st_mode) ||
	    major(st.st_rdev) != major_number || minor(st.st_rdev) != vt - 1) {
		close(fd);
		errno = ENODEV;
		return (-1);
	}
	return (fd);
}

static int
fixture_record(const char *path)
{
	static const char magic[] = "USL_VT_VM_FIXTURE_V1\n";
	struct stat st;
	char data[sizeof(magic)];
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	ssize_t n;
	if (fd < 0)
		return (-1);
	if (fstat(fd, &st) || fixture_stat(&st)) {
		close(fd);
		errno = EINVAL;
		return (-1);
	}
	n = read(fd, data, sizeof(data) - 1);
	if (n != sizeof(magic) - 1 || memcmp(data, magic, sizeof(magic) - 1)) {
		close(fd);
		errno = EINVAL;
		return (-1);
	}
	close(fd);
	return (0);
}

static int
wsdisplay_major(devmajor_t *number)
{
	struct kinfo_drivers *drivers;
	size_t bytes = 0, i;
	int matches = 0;
	if (sysctlbyname("kern.drivers", NULL, &bytes, NULL, 0) || !bytes)
		return (-1);
	drivers = malloc(bytes);
	if (drivers == NULL)
		return (-1);
	if (sysctlbyname("kern.drivers", drivers, &bytes, NULL, 0)) {
		free(drivers);
		return (-1);
	}
	for (i = 0; i < bytes / sizeof(*drivers); i++)
		if (!strncmp(drivers[i].d_name, "wsdisplay",
			sizeof(drivers[i].d_name))) {
			*number = drivers[i].d_cmajor;
			matches++;
		}
	free(drivers);
	if (matches != 1) {
		errno = ENODEV;
		return (-1);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	struct options options;
	const char *fixture, *screen, *back;
	int vt, original, i, control = -1, fd = -1, backfd = -1;
	int result = 1, cleanup_allowed = 0, status, reset_error = 0;
	devmajor_t driver;
	uid_t uid;
	gid_t gid;
	const struct passwd *pw;
	struct stat st;
	struct vt_stat initial;
	struct vt_mode mode;
	u_int display_mode;
	struct actor owner = {.pid = -1, .socket = -1}, peer = owner;
	struct utsname identity;
	if (parse_options(argc, argv, &options))
		goto usage;
	fixture = options.fixture;
	screen = options.screen;
	back = options.back;
	vt = options.vt;
	original = options.original;
	uid = (uid_t)options.uid;
	if (geteuid() != 0 || getuid() != 0) {
		record("requires-root", EPERM);
		return (1);
	}
	pw = getpwuid(uid);
	if (pw == NULL || pw->pw_uid != uid || uid == 0) {
		record("unknown-nonroot-uid", EINVAL);
		return (1);
	}
	gid = pw->pw_gid;
	if (getgrgid(gid) == NULL) {
		record("unknown-primary-group", EINVAL);
		return (1);
	}
	if (fixture_record(fixture)) {
		record("invalid-fixture-record", errno);
		return (1);
	}
	if (uname(&identity))
		return (1);
	printf("kernel %s %s %s\n", identity.sysname, identity.release,
	    identity.version);
	if (wsdisplay_major(&driver)) {
		record("wsdisplay-driver-query", errno);
		return (1);
	}
	control =
	    open("/dev/ttyEcfg", O_RDWR | O_NOFOLLOW | O_NOCTTY | O_NONBLOCK);
	if (control < 0 || fstat(control, &st) || !S_ISCHR(st.st_mode) ||
	    major(st.st_rdev) != driver || minor(st.st_rdev) != 255 ||
	    ioctl(control, VT_GETSTATE, &initial) ||
	    initial.v_active != original ||
	    (initial.v_state & ((1U << vt) | (1U << original)))) {
		record("fixture-not-idle-before-screen-open", errno);
		goto cleanup;
	}
	backfd = screen_fd(back, driver, original);
	fd = screen_fd(screen, driver, vt);
	if (backfd < 0 || fd < 0) {
		record("screen-open", errno);
		goto cleanup;
	}
	for (i = 0; i < 2; i++) {
		int selected = i ? backfd : fd;
		memset(&mode, 0, sizeof(mode));
		if (ioctl(selected, VT_GETMODE, &mode) ||
		    mode.mode != VT_AUTO ||
		    ioctl(selected, WSDISPLAYIO_GMODE, &display_mode) ||
		    display_mode != WSDISPLAYIO_MODE_EMUL) {
			record("fixture-not-auto-text", errno);
			goto cleanup;
		}
	}
	if (set_signal(SIGPIPE, SIG_IGN) || set_signal(SIGINT, on_signal) ||
	    set_signal(SIGTERM, on_signal) || set_signal(SIGHUP, on_signal) ||
	    set_signal(SIGUSR1, on_signal) || set_signal(SIGUSR2, on_signal)) {
		record("signal-setup", errno);
		goto cleanup;
	}
	if (spawn_actor(&owner, fd, backfd, control, -1, uid, gid) ||
	    spawn_actor(&peer, fd, backfd, control, owner.socket, uid, gid))
		goto cleanup;
#define CHECK(expr, name)                                                      \
	do {                                                                   \
		if ((expr) != 0) {                                             \
			record(name, errno);                                   \
			goto cleanup;                                          \
		}                                                              \
		record(name, 0);                                               \
	} while (0)
	cleanup_allowed = 1;
	CHECK(command(&owner, PROCESS, 0, 0), "owner-register");
	CHECK(command(&owner, PROCESS, 0, EBUSY), "owner-duplicate-busy");
	CHECK(command(&peer, PROCESS, 0, EBUSY), "foreign-duplicate-busy");
	CHECK(command(&peer, READ_MODE, 0, 0), "foreign-getmode-readable");
	CHECK(command(&peer, AUTO, 0, EPERM), "foreign-auto-rejected");
	CHECK(ioctl(control, VT_ACTIVATE, vt), "activate-test");
	CHECK(command(&owner, WAIT_SIGNAL, SIGUSR2, 0), "acquire-signal");
	CHECK(
	    command(&peer, ACK, VT_ACKACQ, EPERM), "foreign-acquire-rejected");
	CHECK(command(&owner, CHILD_ACK, VT_ACKACQ, EPERM),
	    "same-uid-child-acquire-rejected");
	CHECK(command(&owner, ACK, VT_ACKACQ, 0), "owner-acquire");
	CHECK(await_active(control, vt), "test-active");
	CHECK(ioctl(control, VT_ACTIVATE, original), "request-release-deny");
	CHECK(command(&owner, WAIT_SIGNAL, SIGUSR1, 0), "release-signal-deny");
	CHECK(command(&peer, ACK, VT_FALSE, EPERM), "foreign-deny-rejected");
	CHECK(command(&owner, CHILD_ACK, VT_FALSE, EPERM),
	    "same-uid-child-deny-rejected");
	CHECK(command(&owner, ACK, VT_FALSE, 0), "owner-deny");
	CHECK(await_active(control, vt), "denial-keeps-test-active");
	CHECK(ioctl(control, VT_ACTIVATE, original), "request-release-allow");
	CHECK(command(&owner, WAIT_SIGNAL, SIGUSR1, 0), "release-signal-allow");
	CHECK(command(&peer, ACK, VT_TRUE, EPERM), "foreign-release-rejected");
	CHECK(command(&owner, CHILD_ACK, VT_TRUE, EPERM),
	    "same-uid-child-release-rejected");
	CHECK(command(&owner, ACK, VT_TRUE, 0), "owner-release");
	CHECK(await_active(control, original), "return-active");
	CHECK(ioctl(control, VT_ACTIVATE, vt), "admin-acquire-request");
	CHECK(command(&owner, WAIT_SIGNAL, SIGUSR2, 0), "admin-acquire-signal");
	CHECK(ioctl(fd, VT_RELDISP, VT_ACKACQ), "admin-acquire-ack");
	CHECK(ioctl(control, VT_ACTIVATE, original), "admin-release-request");
	CHECK(command(&owner, WAIT_SIGNAL, SIGUSR1, 0), "admin-release-signal");
	CHECK(ioctl(fd, VT_RELDISP, VT_TRUE), "admin-release-ack");
	CHECK(await_active(control, original), "admin-return-active");
	mode = (struct vt_mode){.mode = VT_AUTO};
	CHECK(ioctl(fd, VT_SETMODE, &mode), "admin-reset");
	CHECK(command(&owner, PROCESS, 0, 0), "owner-register-before-exit");
	{
		struct packet exit_command = {.op = EXIT_ACTOR};
		CHECK(transfer(
			  owner.socket, &exit_command, sizeof(exit_command), 1),
		    "owner-exit-request");
	}
	CHECK(reap(owner.pid, COMMAND_MS, &status), "owner-reaped");
	owner.pid = -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		goto cleanup;
	{
		int64_t deadline = now_ms() + COMMAND_MS;
		struct timespec delay = {0, 1000000};
		while (own_mode(fd) == 1 && now_ms() < deadline)
			(void)nanosleep(&delay, NULL);
		CHECK(own_mode(fd), "dead-lease-auto");
	}
	CHECK(command(&peer, ACK, VT_TRUE, EINVAL),
	    "retained-foreign-fd-no-dead-lease");
	close(owner.socket);
	owner.socket = -1;
	CHECK(spawn_actor(&owner, fd, backfd, control, peer.socket, uid, gid),
	    "replacement-process");
	CHECK(command(&owner, PROCESS, 0, 0), "replacement-register");
	CHECK(command(&peer, ACK, VT_TRUE, EPERM),
	    "old-fd-actor-cannot-control-replacement");
	CHECK(command(&owner, AUTO, 0, 0), "owner-reset");
	result = 0;
cleanup:
	/*
	 * Only fixture isolation authorizes cleanup. GETMODE has no owner
	 * identity.
	 */
	if (interrupted)
		record("supervisor-interrupted", interrupted);
	interrupted = 0;
	if (stop_child(owner.pid))
		reset_error = 1;
	if (stop_child(peer.pid))
		reset_error = 1;
	if (cleanup_allowed &&
	    restore_fixture(control, fd, backfd, vt, original))
		reset_error = 1;
	if (owner.socket >= 0)
		close(owner.socket);
	if (peer.socket >= 0)
		close(peer.socket);
	if (fd >= 0)
		close(fd);
	if (backfd >= 0)
		close(backfd);
	if (control >= 0)
		close(control);
	record(reset_error	 ? "RESTORE-FAILED"
	       : cleanup_allowed ? "restore-complete"
				 : "cleanup-not-armed",
	    reset_error);
	if (result == 0 && reset_error == 0)
		record("PASS-normal-ioctl-owner-admin-exit", 0);
	return (result || reset_error);
usage:
	fprintf(stderr, "Usage: usl-vt-vm --vm-fixture --fixture-record "
			"ROOT600FILE --screen /dev/ttyE<N> --return-screen "
			"/dev/ttyE<M> --uid NONROOT_UID\n");
	return (2);
}
#endif
