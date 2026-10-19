/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source boundary regressions. */
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

#if defined(TEST_HACK) || defined(TEST_RAID)
static size_t
copy_string(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);
	if (size != 0) {
		size_t count = len < size - 1 ? len : size - 1;
		memcpy(dst, src, count);
		dst[count] = '\0';
	}
	return len;
}
#define strlcpy copy_string
#endif

#ifdef TEST_FUNOPEN
typedef struct {
	void *cookie;
	int (*readfn)(void *, char *, int);
	int (*writefn)(void *, const char *, int);
	off_t (*seekfn)(void *, off_t, int);
	int (*closefn)(void *);
} dookie_t;
/* @FUNOPEN@ */
struct sink { char data[32]; size_t used; int fail_after; };
static int
short_write(void *cookie, const char *data, int count)
{
	struct sink *sink = cookie;
	if (sink->fail_after >= 0 && (int)sink->used >= sink->fail_after)
		return -1;
	if (count > 3)
		count = 3;
	CHECK(sink->used + (size_t)count <= sizeof(sink->data));
	memcpy(sink->data + sink->used, data, (size_t)count);
	sink->used += (size_t)count;
	return count;
}
int
main(void)
{
	struct sink sink = { .fail_after = -1 };
	dookie_t d = { .cookie = &sink, .writefn = short_write };
	const char input[] = "abcdefghijkl";
	CHECK(cwritefn(&d, input, 12) == 12);
	CHECK(sink.used == 12 && memcmp(sink.data, input, 12) == 0);
	sink = (struct sink){ .fail_after = 6 };
	CHECK(cwritefn(&d, input, 12) == 6);
	CHECK(sink.used == 6 && memcmp(sink.data, input, 6) == 0);
	sink = (struct sink){ .fail_after = 0 };
	CHECK(cwritefn(&d, input, 12) == -1);
	CHECK(cwritefn(&d, input, 0) == 0 && sink.used == 0);
	puts("PASS: full/partial/failed/empty legacy writes");
}
#endif

#ifdef TEST_MAIL
struct name { struct name *n_flink, *n_blink; char *n_name; int n_type; };
struct smopts_s { struct smopts_s *s_link; char *s_name; struct name *s_smopts; };
static struct smopts_s *smoptstbl[1];
static void *allocations[32];
static size_t live;
static void *
own(size_t size)
{
	void *p = calloc(1, size);
	CHECK(p != NULL && live < 32);
	allocations[live++] = p;
	return p;
}
static char *
own_string(const char *str)
{
	char *p = own(strlen(str) + 1);
	return strcpy(p, str);
}
static void
release(void *p)
{
	size_t i;
	for (i = 0; i < live && allocations[i] != p; i++)
		continue;
	CHECK(i < live);
	allocations[i] = allocations[--live];
	free(p);
}
static int hashcase(const char *str) { (void)str; return 0; }
#define free release
/* @MAIL@ */
#undef free
static struct smopts_s *
entry(const char *str, struct smopts_s *next)
{
	struct smopts_s *sp = own(sizeof(*sp));
	sp->s_name = own_string(str);
	sp->s_link = next;
	sp->s_smopts = own(sizeof(*sp->s_smopts));
	sp->s_smopts->n_name = own_string("first");
	sp->s_smopts->n_flink = own(sizeof(*sp->s_smopts));
	sp->s_smopts->n_flink->n_name = own_string("second");
	return sp;
}
int
main(void)
{
	struct smopts_s *tail = entry("tail", NULL);
	struct smopts_s *middle = entry("middle", tail);
	struct smopts_s *head = entry("head", middle);
	smoptstbl[0] = head;
	delsmopts("missing");
	CHECK(live == 18 && smoptstbl[0] == head);
	delsmopts("MIDDLE");
	CHECK(live == 12 && smoptstbl[0] == head && head->s_link == tail);
	delsmopts("head");
	CHECK(live == 6 && smoptstbl[0] == tail);
	delsmopts("tail");
	CHECK(live == 0 && smoptstbl[0] == NULL);
	delsmopts("tail");
	puts("PASS: colliding mail options, case folding and complete ownership cleanup");
}
#endif

#ifdef TEST_TIP
static int fildes[2] = { 10, 11 }, repdes[2] = { 12, 13 };
static FILE *fscript;
static bool scripting;
static const char *input;
static size_t consumed, read_calls;
static bool interrupt_once, fail_read;
static unsigned opens, closes, replies;
static char wire_reply, opened[256];
#define SCRIPT 0
#define FALSE false
#define TRUE true
#define value(x) (scripting)
#define boolean(x) (x)
#define setboolean(x, v) ((x) = (v))
static ssize_t
input_read(int fd, void *data, size_t size)
{
	CHECK(fd == fildes[0] && size == 1);
	CHECK(++read_calls < 2048);
	if (interrupt_once) {
		interrupt_once = false;
		errno = EINTR;
		return -1;
	}
	if (fail_read) { errno = EIO; return -1; }
	if (input[consumed] == '\0')
		return 0;
	*(char *)data = input[consumed++];
	return 1;
}
static ssize_t
reply_write(int fd, const void *data, size_t size)
{
	CHECK(fd == repdes[1] && size == 1);
	wire_reply = *(const char *)data;
	replies++;
	return 1;
}
static FILE *
open_script(const char *path, const char *mode)
{
	CHECK(strcmp(mode, "a") == 0 && strlen(path) < sizeof(opened));
	strcpy(opened, path);
	opens++;
	return stdout;
}
static int close_script(FILE *fp) { CHECK(fp == stdout); closes++; return 0; }
#define read input_read
#define write reply_write
#define fopen open_script
#define fclose close_script
/* @TIP@ */
#undef read
#undef write
#undef fopen
#undef fclose
static void
reset_input(const char *data)
{
	input = data; consumed = read_calls = 0;
	opens = closes = replies = 0;
	wire_reply = '\0'; opened[0] = '\0';
	scripting = true; fscript = stdout;
	interrupt_once = fail_read = false;
}
int
main(void)
{
	char data[320];
	reset_input("record\n"); interrupt_once = true; intEMT();
	CHECK(wire_reply == 'y' && replies == 1 && opens == 1 && closes == 1);
	CHECK(strcmp(opened, "record") == 0 && scripting);
	reset_input("\n"); intEMT();
	CHECK(wire_reply == 'y' && replies == 1 && opens == 0 && closes == 1 && !scripting);
	memset(data, 'x', 255); strcpy(data + 255, "\n");
	reset_input(data); intEMT();
	CHECK(wire_reply == 'y' && opens == 1 && strlen(opened) == 255 && consumed == 256);
	memset(data, 'x', 256); strcpy(data + 256, "\nnext\n");
	reset_input(data); intEMT();
	CHECK(wire_reply == 'n' && replies == 1 && opens == 0 && closes == 0);
	CHECK(consumed == 257 && scripting && fscript == stdout);
	intEMT();
	CHECK(wire_reply == 'y' && replies == 2 && opens == 1 && strcmp(opened, "next") == 0);
	reset_input("partial"); intEMT();
	CHECK(wire_reply == 'n' && opens == 0 && closes == 0 && scripting);
	reset_input(""); intEMT();
	CHECK(wire_reply == 'n' && opens == 0 && replies == 1);
	reset_input("ignored"); fail_read = true; intEMT();
	CHECK(wire_reply == 'n' && opens == 0 && replies == 1);
	puts("PASS: tip interrupted/failed/EOF reads and complete command boundaries");
}
#endif

#ifdef TEST_HACK
#define BUFSZ 256
static char toplines[BUFSZ];
static int CO, tlx, tly;
static struct { int toplin; } flags;
static void nscr(void) { }
static void more(void) { }
static void remember_topl(void) { }
static void redotoplin(void) { }
static void addtopl(const char *p) { (void)p; }
static int letter(int c) { return isalpha((unsigned char)c) || c == '_'; }
static size_t
append_string(char *dst, const char *src, size_t size)
{
	size_t old = strlen(dst);
	CHECK(old < size);
	return old + copy_string(dst + old, src, size - old);
}
#define strlcat append_string
/* @HACK@ */
static void message(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vpline(format, ap);
	va_end(ap);
}
int
main(void)
{
	const int widths[] = { 0, 1, 2, 3, 8, 80 };
	for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		CO = widths[i]; flags.toplin = 0;
		message("alphabet soup");
		CHECK(strlen(toplines) < sizeof(toplines));
		if (CO < 3) CHECK(toplines[0] == '\0');
		if (CO == 80) CHECK(strcmp(toplines, "alphabet soup") == 0);
	}
	puts("PASS: narrow terminal wrap and ordinary message");
}
#endif

#ifdef TEST_LARN
#define MAXLEVEL 10
#define MAXVLEVEL 3
static int level, next_roll;
enum { OSCROLL=1, OPOTION, OGOLDPILE, OBOOK, ODAGGER, OLEATHER,
    OREGENRING, OPROTRING, OENERGYRING, ODEXRING, OSTRRING, OSPEAR,
    OBELT, ORING, OSTUDLEATHER, OSHIELD, OFLAIL, OCHAIN, O2SWORD,
    OPLATE, OLONGSWORD };
static int rnd(int n) { int result = next_roll; next_roll = 1; CHECK(result <= n); return result; }
static int rund(int n) { CHECK(n > 0); return 0; }
static int newscroll(void) { return 12; }
static int newpotion(void) { return 13; }
static int newdagger(void) { return 14; }
static int newleather(void) { return 15; }
static int newchain(void) { return 16; }
static int newplate(void) { return 17; }
static int newsword(void) { return 18; }
/* @LARN@ */
int
main(void)
{
	int argument = 1234;
	level = -1;
	CHECK(newobject(1, &argument) == 0 && argument == 0);
	level = MAXLEVEL + MAXVLEVEL + 1; argument = 1234;
	CHECK(newobject(1, &argument) == 0 && argument == 0);
	level = 1; next_roll = 1;
	CHECK(newobject(1, &argument) == OSCROLL && argument == 12);
	next_roll = 28; argument = 1234;
	CHECK(newobject(1, &argument) == 0 && argument == 0);
	puts("PASS: invalid/no-object output and ordinary generated object");
}
#endif

#ifdef TEST_KGMON
int
main(void)
{
	/* @KGMON_DECL@ */
	char expected[128];
	const int ids[] = { 0, 100, INT_MAX, INT_MIN };
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		int cpuid = ids[i];
		/* @KGMON_FORMAT@ */
		snprintf(expected, sizeof(expected), " on cpu%d", cpuid);
		CHECK(strcmp(on_cpu, expected) == 0);
	}
	puts("PASS: complete signed CPU label");
}
#endif

#ifdef TEST_PAC
struct hent { struct hent *h_link; char *h_name; float h_feetpages; int h_count; };
static struct hent *hashtab[97];
static int hcount, mflag, allflag;
static float price = 0.02;
static int qucmp(const void *a, const void *b)
{
	return strcmp((*(struct hent *const *)a)->h_name, (*(struct hent *const *)b)->h_name);
}
static void *
pointer_array(size_t count, size_t width)
{
	CHECK(count == (size_t)hcount && width == sizeof(struct hent *));
	return calloc(count, width);
}
static void err(int code, const char *message) { (void)message; exit(code); }
#define calloc pointer_array
/* @PAC@ */
#undef calloc
int
main(void)
{
	struct hent b = { NULL, "b", 2, 2 }, a = { &b, "a", 1, 1 };
	hashtab[0] = &a; hcount = 2; dumpit();
	hashtab[0] = NULL;
	puts("PASS: accounting pointer-array allocation and traversal");
}
#endif

#ifdef TEST_RAID
/* @RAID@ */
int
main(void)
{
	char out[40];
	char control[2];
	CHECK(copy_string(control, "x", sizeof(control)) == 1);
	get_bar(out, 0, sizeof(out));
	CHECK(strlen(out) == 39 && strspn(out, " ") == 39);
	get_bar(out, 50, sizeof(out));
	CHECK(strlen(out) == 39 && strspn(out, "*") == 20);
	get_bar(out, 100, sizeof(out));
	CHECK(strlen(out) == 39 && strspn(out, "*") == 39);
	puts("PASS: bounded progress bar at 0/50/100 percent");
}
#endif

#ifdef TEST_CSH
#define MAXPATHLEN 1024
#define GLOBSPACE 100
#define ERR_UNMATCHED 1
#define ERR_WTOOLONG 2
typedef short Char;
static Char **pargv;
long pargc, pnleft;
Char *pargcp, *pargs;
static int pargsiz;
static void backeval(Char *, int);
static void psave(int);
static void pword(void);
static void stderror(int code, ...) { exit(100 + code); }
static Char *
Strsave(const Char *text)
{
	size_t len = 0;
	while (text[len]) len++;
	Char *copy = malloc((len + 1) * sizeof(*copy));
	CHECK(copy != NULL);
	return memcpy(copy, text, (len + 1) * sizeof(*copy));
}
static void
blkfree(Char **words)
{
	for (size_t i = 0; words[i]; i++) free(words[i]);
	free(words);
}
static void *xreallocarray(void *p, size_t count, size_t width)
{
	CHECK(count <= SIZE_MAX / width);
	void *q = realloc(p, count * width);
	CHECK(q != NULL);
	return q;
}
/* @CSH@ */
/* The real backeval forks before command parsing. The fixture retains this
 * process boundary and substitutes only the command evaluator and its output. */
static void
backeval(Char *command, int literal)
{
	int pipefd[2], status;
	pid_t child;
	(void)literal;
	CHECK(pipe(pipefd) == 0);
	CHECK((child = fork()) >= 0);
	if (child == 0) {
		const Char nested[] = { 'i','n','s','i','d','e',0 };
		close(pipefd[0]);
		/* This evaluator double consumes the inherited synthetic command. */
		free(command);
		Char **result = dobackp((Char *)nested, 0);
		for (size_t i = 0; result[0][i]; i++) {
			char byte = (char)result[0][i];
			CHECK(write(pipefd[1], &byte, 1) == 1);
		}
		blkfree(pargv); pargv = NULL;
		close(pipefd[1]);
		_exit(0);
	}
	free(command);
	close(pipefd[1]);
	char byte;
	ssize_t got;
	while ((got = read(pipefd[0], &byte, 1)) > 0) psave(byte);
	CHECK(got == 0);
	close(pipefd[0]);
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void
word_equals(const Char *word, const char *expected)
{
	for (; *expected; expected++, word++) CHECK(*word == *expected);
	CHECK(*word == 0);
}
int
main(void)
{
	Char first[] = { 'a','b','c',0 };
	Char nested[] = { 'p','r','e','`','x','`','p','o','s','t',0 };
	for (int i = 0; i < 3; i++) {
		Char **out = dobackp(first, 0);
		CHECK(pargc == 1); word_equals(out[0], "abc");
		out = dobackp(nested, 0);
		CHECK(pargc == 1); word_equals(out[0], "preinsidepost");
	}
	blkfree(pargv); pargv = NULL;
	puts("PASS: repeated and fork-isolated nested substitutions");
}
#endif
