/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted actual-source tests. */
/* All process, reboot, filesystem and time boundaries are modeled. */
#define _DEFAULT_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <syslog.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static unsigned int checks;
static void
check(bool value, unsigned int line, const char *expression)
{
    checks++;
    if (!value) {
        fprintf(stderr, "FAIL line %u: %s\n", line, expression);
        exit(1);
    }
}
#define CHECK(c) check((c), __LINE__, #c)
#define lint 1
#undef __dead
#define __dead __attribute__((noreturn))
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define TM_YEAR_BASE 1900
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif
#define RB_HALT 8
#define RB_POWERDOWN (8|0x800)
#define RB_NOSYNC 4
#define RB_DUMP 0x100
#define RB_STRING 0x400
#define AB_VERBOSE 0x20000
#define AB_DEBUG 0x80000
#define AB_SILENT 0x40000
#define _PATH_NOLOGIN "/etc/nologin"
#define _PATH_FASTBOOT "/fastboot"
#define _PATH_HALT "/sbin/halt"
#define _PATH_REBOOT "/sbin/reboot"
#define _PATH_WALL "/usr/bin/wall"
#define _PATH_RCSHUTDOWN "/etc/rc.shutdown"
#ifndef timespeccmp
/* Native header primitive, when the host does not expose it. */
#define timespeccmp(a, b, op) \
    ((a)->tv_sec == (b)->tv_sec ? (a)->tv_nsec op (b)->tv_nsec : \
    (a)->tv_sec op (b)->tv_sec)
#endif
#ifndef timespecsub
#define timespecsub(a, b, out) do { \
    (out)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (out)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
    if ((out)->tv_nsec < 0) { (out)->tv_sec--; (out)->tv_nsec += 1000000000; } \
} while (0)
#endif

static jmp_buf boundary;
static int result, reboot_calls, reboot_flags, exec_calls, hook_calls, sync_calls;
static int term_calls, kill_calls, probes, clock_calls, sleep_calls, restart_calls;
static int probe_fail, clock_fail, nanosleep_error, term_error, kill_error;
static int eintr_count, expected_uid;
static int exec_error, fork_calls;
static int open_result;
static bool malloc_error;
static void *allocated_boot;
static bool clock_error_after_sync, clock_error_after_kill;
static bool persistent, spawn_on_kill, empty_seen;
static bool spawn_after_kill_empty, pending_spawn;
static uint64_t now_ns, exit_ns, kill_clear_ns;
static unsigned int sleep_values[32];
static uint64_t kill_times[8], sync_times[8];
static char events[4096], exec_args[256];
static size_t event_count;
static const char *program;
static void event(char c) { CHECK(event_count < sizeof(events)-1); events[event_count++] = c; }
_Noreturn void model_exit(int n) { result = n; longjmp(boundary, 1); }
_Noreturn void model_err(int n, const char *format, ...)
{ (void)format; model_exit(n); }
_Noreturn void model_errx(int n, const char *format, ...)
{ (void)format; model_exit(n); }
void model_warn(const char *format, ...) { (void)format; event('W'); }
void model_warnx(const char *format, ...) { (void)format; event('W'); }
int model_printf(const char *format, ...) { (void)format; return 0; }
int model_fprintf(FILE *stream, const char *format, ...)
{ (void)stream; (void)format; return 0; }
size_t model_fwrite(const void *p, size_t size, size_t n, FILE *stream)
{ (void)p; (void)stream; (void)size; return n; }
void model_syslog(int priority, const char *format, ...)
{ (void)priority; (void)format; event('L'); }
void model_openlog(const char *s, int a, int b) { (void)s; (void)a; (void)b; }
void model_setprogname(const char *s) { program = s; }
const char *model_getprogname(void) { return program; }
uid_t model_geteuid(void) { return (uid_t)expected_uid; }
uid_t model_getuid(void) { return (uid_t)expected_uid; }
char *model_getlogin(void) { return "fixture"; }
struct passwd *model_getpwuid(uid_t uid)
{ static struct passwd pw; (void)uid; pw.pw_name = "fixture"; return &pw; }
int model_gethostname(char *p, size_t n) { if (n) p[0] = 0; return 0; }
int model_setpriority(int a, id_t b, int c) { (void)a; (void)b; (void)c; return 0; }
int model_setuid(uid_t uid) { (void)uid; return 0; }
pid_t model_fork(void) { fork_calls++; return 0; }
pid_t model_setsid(void) { return 42; }
unsigned int model_alarm(unsigned int n) { (void)n; return 0; }
void (*model_signal(int n, void (*handler)(int)))(int)
{ (void)n; (void)handler; return SIG_DFL; }
int model_unlink(const char *path) { (void)path; return 0; }
int model_open(const char *path, int flags, ...)
{ (void)path; (void)flags; return open_result; }
int model_close(int fd) { (void)fd; return 0; }
ssize_t model_write(int fd, const void *p, size_t n)
{ (void)fd; (void)p; return (ssize_t)n; }
FILE *model_popen(const char *command, const char *mode)
{ (void)command; (void)mode; return (FILE *)(uintptr_t)1; }
int model_pclose(FILE *stream) { (void)stream; return 0; }
int model_system(const char *command)
{ CHECK(strstr(command, "/etc/rc.shutdown") != NULL); hook_calls++; event('H'); return 0; }
int model_execve(const char *path, char *const args[], char *const env[])
{
    size_t i;
    CHECK(strcmp(path, "/sbin/halt") == 0 || strcmp(path, "/sbin/reboot") == 0);
    (void)env;
    exec_calls++;
    for (i = 0; args[i] != NULL; i++) {
        CHECK(strlen(exec_args) + strlen(args[i]) + 2 < sizeof(exec_args));
        strcat(exec_args, args[i]); strcat(exec_args, " ");
    }
    event('E');
    if (exec_error) { errno = exec_error; return -1; }
    model_exit(0);
}
int model_reboot(int how, const char *boot)
{ (void)boot; reboot_calls++; reboot_flags = how; event('R'); model_exit(0); }
void *model_malloc(size_t size)
{
    if (malloc_error) return NULL;
    CHECK(allocated_boot == NULL);
    allocated_boot = malloc(size);
    CHECK(allocated_boot != NULL);
    return allocated_boot;
}
void model_sync(void)
{ CHECK(sync_calls < 8); sync_times[sync_calls++] = now_ns; event('Y'); }
unsigned int model_sleep(unsigned int n)
{
    CHECK(sleep_calls < 32); sleep_values[sleep_calls++] = n;
    now_ns += (uint64_t)n * 1000000000; event('S'); return 0;
}
int model_clock_gettime(clockid_t clock, struct timespec *out)
{
    CHECK(clock == CLOCK_MONOTONIC);
    if (++clock_calls == clock_fail || (clock_error_after_sync && sync_calls == 2) ||
        (clock_error_after_kill && kill_calls != 0)) { errno = EIO; return -1; }
    out->tv_sec = (time_t)(now_ns / 1000000000);
    out->tv_nsec = (long)(now_ns % 1000000000);
    return 0;
}
int model_nanosleep(const struct timespec *request, struct timespec *remain)
{
    uint64_t duration;
    (void)remain;
    CHECK(request->tv_sec == 0 && request->tv_nsec > 0 && request->tv_nsec <= 100000000);
    duration = (uint64_t)request->tv_nsec;
    if (nanosleep_error) { errno = EIO; return -1; }
    if (eintr_count) {
        eintr_count--; now_ns += duration / 2; errno = EINTR; return -1;
    }
    now_ns += duration;
    return 0;
}
int model_kill(pid_t pid, int sig)
{
    if (pid == 1) {
        CHECK(sig == SIGTSTP || sig == SIGHUP);
        if (sig == SIGHUP) { restart_calls++; event('A'); }
        else event('I');
        return 0;
    }
    CHECK(pid == -1);
    if (sig == SIGTERM) {
        term_calls++; event('T');
        if (term_error) { errno = term_error; return -1; }
        return 0;
    }
    if (sig == SIGKILL) {
        CHECK(kill_calls < 8); kill_times[kill_calls++] = now_ns; event('K');
        if (kill_error) { errno = kill_error; return -1; }
        if (spawn_on_kill && empty_seen) { persistent = true; spawn_on_kill = false; }
        if (pending_spawn) { persistent = true; pending_spawn = false; }
        if (!persistent && (now_ns >= exit_ns ||
            (kill_clear_ns != 0 && now_ns >= kill_clear_ns))) { errno = ESRCH; return -1; }
        if (kill_clear_ns == 0) kill_clear_ns = now_ns + 100000000;
        return 0;
    }
    CHECK(sig == 0);
    probes++;
    if (probes == probe_fail) { errno = EIO; return -1; }
    if ((!persistent && now_ns >= exit_ns) ||
        (kill_clear_ns != 0 && now_ns >= kill_clear_ns && !persistent)) {
        empty_seen = true;
        if (spawn_after_kill_empty && kill_calls != 0) {
            pending_spawn = true; spawn_after_kill_empty = false;
        }
        errno = ESRCH; return -1;
    }
    return 0;
}

/* Every dangerous host service is replaced before including the full TU. */
#define exit model_exit
#define err model_err
#define errx model_errx
#define warn model_warn
#define warnx model_warnx
#define printf model_printf
#define fprintf model_fprintf
#define fwrite model_fwrite
#define syslog model_syslog
#define openlog model_openlog
#define setprogname model_setprogname
#define getprogname model_getprogname
#define geteuid model_geteuid
#define getuid model_getuid
#define getlogin model_getlogin
#define getpwuid model_getpwuid
#define gethostname model_gethostname
#define setpriority model_setpriority
#define setuid model_setuid
#define fork model_fork
#define setsid model_setsid
#define alarm model_alarm
#define signal model_signal
#define unlink model_unlink
#define open model_open
#define close model_close
#define write model_write
#define popen model_popen
#define pclose model_pclose
#define system model_system
#define execve model_execve
#define reboot model_reboot
#define sync model_sync
#define sleep model_sleep
#define clock_gettime model_clock_gettime
#define nanosleep model_nanosleep
#define kill model_kill
#define malloc model_malloc
#define main native_main
#include "native-source.inc"
#undef main
#undef printf
#undef fprintf
#undef exit

static void
reset_case(void)
{
    free(allocated_boot); allocated_boot = NULL; malloc_error = false;
    result = reboot_calls = reboot_flags = exec_calls = hook_calls = sync_calls = 0;
    term_calls = kill_calls = probes = clock_calls = sleep_calls = restart_calls = 0;
    probe_fail = clock_fail = nanosleep_error = term_error = kill_error = 0;
    eintr_count = expected_uid = 0;
    exec_error = fork_calls = 0;
    open_result = 9;
    clock_error_after_sync = clock_error_after_kill = false;
    persistent = spawn_on_kill = empty_seen = false;
    spawn_after_kill_empty = pending_spawn = false;
    now_ns = 0; exit_ns = 300000000; kill_clear_ns = 0;
    event_count = 0; memset(events, 0, sizeof(events)); exec_args[0] = 0;
#ifdef TEST_SHUTDOWN
    offset = shuttime = 0;
    dofast = dohalt = doreboot = killflg = nofork = nosync = dodump = 0;
    dopowerdown = dodebug = dosilent = doverbose = 0;
    doimmediate = 0;
    mbuflen = 0; mbuf[0] = 0; bootstr = NULL;
#else
    dohalt = dopoweroff = 0;
#endif
#if defined(__APPLE__)
    optreset = 1; optind = 1;
#else
    optind = 0;
#endif
    opterr = 0;
}
static void
run(char **argv)
{
    volatile int argc = 0;
    while (argv[argc]) argc++;
    program = argv[0];
    if (setjmp(boundary) == 0) {
        (void)native_main(argc, argv);
        CHECK(false);
    }
}
int
main(void)
{
#ifdef TEST_SHUTDOWN
    char *immediate[] = { "shutdown", "-D", "-i", "-p", "now", "button", NULL };
    char *normal[] = { "shutdown", "-p", "now", NULL };
    char *bad_n[] = { "shutdown", "-i", "-n", "-p", "now", NULL };
    char *bad_f[] = { "shutdown", "-i", "-f", "-p", "now", NULL };
    char *bad_k[] = { "shutdown", "-i", "-k", "-p", "now", NULL };
    char *future[] = { "shutdown", "-i", "-p", "+1", NULL };
    char *single[] = { "shutdown", "-i", "now", NULL };
    char **invalid[] = { bad_n, bad_f, bad_k, future, single };
    size_t i;
    reset_case(); run(immediate);
    CHECK(result == 0 && exec_calls == 1 && hook_calls == 1);
    CHECK(fork_calls == 0);
    CHECK(strcmp(exec_args, "halt -i -p -l ") == 0);
    CHECK(now_ns == 0 && sleep_calls == 1 && sleep_values[0] == 0);
    reset_case(); run(normal);
    CHECK(result == 0 && strcmp(exec_args, "halt -p -l ") == 0);
    CHECK(hook_calls == 1 && now_ns == 7000000000 && sleep_calls == 3);
    CHECK(sleep_values[0] == 0 && sleep_values[1] == 2 && sleep_values[2] == 5);
    CHECK(fork_calls == 1);
    reset_case(); exec_error = ENOENT; run(normal);
    CHECK(result == 0 && exec_calls == 1 && hook_calls == 1);
    reset_case(); exec_error = ENOENT; run(immediate);
    CHECK(result == 1 && exec_calls == 1 && hook_calls == 1 && fork_calls == 0);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        reset_case(); run(invalid[i]);
        CHECK(result != 0 && exec_calls == 0 && hook_calls == 0);
    }
    reset_case(); expected_uid = 1000; run(immediate);
    CHECK(result != 0 && exec_calls == 0 && hook_calls == 0);
    CHECK(term_calls == 0 && kill_calls == 0 && reboot_calls == 0);
#else
    char *immediate[] = { "halt", "-i", "-p", NULL };
    char *normal[] = { "halt", "-p", NULL };
    char *bad_n[] = { "halt", "-i", "-n", "-p", NULL };
    char *bad_q[] = { "halt", "-i", "-q", "-p", NULL };
    char *with_boot[] = { "halt", "-i", "-p", "bootarg", NULL };
    size_t i;
    reset_case(); run(immediate);
    CHECK(result == 0 && reboot_calls == 1 && reboot_flags == RB_POWERDOWN);
    CHECK(sync_calls == 2 && term_calls == 1 && kill_calls == 1 && probes > 0);
    CHECK(now_ns == 300000000 && sleep_calls == 0 && restart_calls == 0);
    CHECK(strcmp(events, "LYITYKR") == 0);
    reset_case(); run(normal);
    CHECK(result == 0 && sync_calls == 2 && probes == 0 && clock_calls == 0);
    CHECK(now_ns == 5000000000 && sleep_calls == 2);
    CHECK(sleep_values[0] == 2 && sleep_values[1] == 3);
    reset_case(); exit_ns = 0; term_error = ESRCH; run(immediate);
    CHECK(result == 0 && now_ns == 0 && sync_calls == 2 && kill_calls == 1);
    reset_case(); exit_ns = 3000000000; run(immediate);
    CHECK(result == 0 && now_ns == 3000000000 && sync_times[1] == 2000000000);
    reset_case(); exit_ns = UINT64_MAX; run(immediate);
    CHECK(result == 0 && now_ns == 5100000000 && kill_calls == 2);
    reset_case(); exit_ns = UINT64_MAX; spawn_after_kill_empty = true; run(immediate);
    CHECK(kill_calls > 1 && !pending_spawn && now_ns >= 30000000000);
    reset_case(); persistent = true; run(normal);
    CHECK(result == 0 && now_ns == 35000000000 && kill_calls == 6);
    CHECK(probes == 0 && clock_calls == 0 && sleep_calls == 7);
    CHECK(sleep_values[2] == 2 && sleep_values[3] == 4 && sleep_values[4] == 6 &&
        sleep_values[5] == 8 && sleep_values[6] == 10);
    reset_case(); persistent = true; run(immediate);
    CHECK(result == 0 && now_ns == 35000000000 && term_calls == 1 && kill_calls == 6);
    CHECK(sync_calls == 2 && sync_times[0] == 0 && sync_times[1] == 2000000000);
    for (i = 0; i < 6; i++) {
        static const uint64_t times[] = {5,7,11,17,25,35};
        CHECK(kill_times[i] == times[i] * 1000000000);
    }
    reset_case(); eintr_count = 8; run(immediate);
    CHECK(result == 0 && now_ns >= exit_ns && now_ns <= exit_ns + 100000000);
    CHECK(kill_calls == 1 && reboot_calls == 1);
    reset_case(); persistent = true; eintr_count = 1; run(immediate);
    CHECK(kill_times[0] == 5000000000 && now_ns == 35000000000);
    reset_case(); spawn_on_kill = true; run(immediate);
    CHECK(kill_calls >= 1 && event_count != 0 && strchr(events, 'K') != NULL);
    CHECK(now_ns >= 30000000000); /* Spawn survives until native KILL cap. */
    for (i = 0; i < 4; i++) {
        reset_case();
        if (i == 0) probe_fail = 1;
        if (i == 1) clock_fail = 1;
        if (i == 2) nanosleep_error = 1;
        if (i == 3) kill_error = EIO;
        run(immediate);
        CHECK(result != 0 && reboot_calls == 0 && restart_calls == 1);
    }
    reset_case(); term_error = EIO; run(immediate);
    CHECK(result != 0 && reboot_calls == 0 && restart_calls == 1);
    reset_case(); persistent = true; clock_error_after_sync = true; run(immediate);
    CHECK(result != 0 && reboot_calls == 0 && restart_calls == 1 && sync_calls == 2);
    reset_case(); exit_ns = UINT64_MAX; clock_error_after_kill = true; run(immediate);
    CHECK(result != 0 && reboot_calls == 0 && restart_calls == 1 && kill_calls == 1);
    reset_case(); run(bad_n); CHECK(result != 0 && reboot_calls == 0 && sync_calls == 0);
    reset_case(); run(bad_q); CHECK(result != 0 && reboot_calls == 0 && sync_calls == 0);
    reset_case(); expected_uid = 1000; run(immediate);
    CHECK(result != 0 && reboot_calls == 0 && sync_calls == 0);
    reset_case(); run(with_boot);
    CHECK(result == 0 && reboot_calls == 1 && (reboot_flags & RB_STRING) != 0);
    reset_case(); malloc_error = true; run(with_boot);
    CHECK(result == 1 && reboot_calls == 0 && sync_calls == 0 && term_calls == 0);
#endif
    printf("immediate shutdown actual-source %s: %u checks, 0 failures\n",
#ifdef TEST_SHUTDOWN
        "shutdown",
#else
        "reboot",
#endif
        checks);
    return 0;
}
