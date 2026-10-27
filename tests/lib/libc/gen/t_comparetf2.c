/* Origin: EmberBSD; AI-assisted AArch64 binary128 comparison FENV regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define _NETBSD_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <dlfcn.h>
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IOC UINT64_C(1)
#define IOE (UINT64_C(1) << 8)
#define TRAPS (UINT64_C(0x9f) << 8)
#define MODES ((UINT64_C(3) << 22) | (UINT64_C(3) << 24))

struct result {
	int value;
	uint64_t fpsr, fpcr;
};
_Static_assert(offsetof(struct result, fpsr) == 8, "raw ABI FPSR layout");
_Static_assert(offsetof(struct result, fpcr) == 16, "raw ABI FPCR layout");
extern void comparetf2_call(void *, const uint64_t *, const uint64_t *,
    struct result *);
extern int comparetf2_capture(void);
extern uint64_t comparetf2_capture_a[2], comparetf2_capture_b[2];

struct operand {
	const char *name;
	uint64_t bits[2];
	int rank, nan;
};
/* IEEE encodings and expected ranks/classes, independent of the comparator. */
static const struct operand operands[] = {
	{ "-inf", {0, UINT64_C(0xffff000000000000)}, -3, 0 },
	{ "-one", {0, UINT64_C(0xbfff000000000000)}, -1, 0 },
	{ "-zero", {0, UINT64_C(0x8000000000000000)}, 0, 0 },
	{ "+zero", {0, 0}, 0, 0 },
	{ "one", {0, UINT64_C(0x3fff000000000000)}, 1, 0 },
	{ "two", {0, UINT64_C(0x4000000000000000)}, 2, 0 },
	{ "+inf", {0, UINT64_C(0x7fff000000000000)}, 3, 0 },
	{ "qNaN", {UINT64_C(0x0123456789abcdef), UINT64_C(0x7fff800000000042)}, 0, 1 },
	{ "sNaN", {UINT64_C(0x0123456789abcdef), UINT64_C(0x7fff000000000042)}, 0, 2 },
	{ "-qNaN-min", {0, UINT64_C(0xffff800000000000)}, 0, 1 },
	{ "-sNaN-min", {1, UINT64_C(0xffff000000000000)}, 0, 2 },
	{ "sNaN-low-min", {1, UINT64_C(0x7fff000000000000)}, 0, 2 },
	{ "sNaN-high-min", {0, UINT64_C(0x7fff000000000001)}, 0, 2 },
	{ "sNaN-max", {UINT64_MAX, UINT64_C(0x7fff7fffffffffff)}, 0, 2 },
	{ "qNaN-max", {UINT64_MAX, UINT64_C(0xffffffffffffffff)}, 0, 1 }
};
static const char *helpers[] = {
	"__eqtf2", "__netf2", "__unordtf2", "__gttf2", "__getf2",
	"__lttf2", "__letf2", "__cmptf2"
};

static uint64_t
read_fpcr(void)
{
	uint64_t x;
	__asm__ volatile("mrs %0, fpcr" : "=r" (x));
	return x;
}

static uint64_t
read_fpsr(void)
{
	uint64_t x;
	__asm__ volatile("mrs %0, fpsr" : "=r" (x));
	return x;
}

static void
write_fpcr(uint64_t x)
{
	__asm__ volatile("msr fpcr, %0\n\tisb" : : "r" (x) : "memory");
}

static void
write_fpsr(uint64_t x)
{
	__asm__ volatile("msr fpsr, %0\n\tisb" : : "r" (x) : "memory");
}

static int
predicate(unsigned h, int value, const struct operand *a,
    const struct operand *b)
{
	int unordered = a->nan || b->nan;
	int cmp = (a->rank > b->rank) - (a->rank < b->rank);

	switch (h) {
	case 0: case 1: return (value == 0) == (!unordered && cmp == 0);
	case 2: return (value != 0) == unordered;
	case 3: return (value > 0) == (!unordered && cmp > 0);
	case 4: return (value >= 0) == (!unordered && cmp >= 0);
	case 5: return (value < 0) == (!unordered && cmp < 0);
	case 6: case 7: return (value <= 0) == (!unordered && cmp <= 0);
	default: abort();
	}
}

static void *
open_helpers(const atf_tc_t *tc, void **functions)
{
	const char *path = atf_tc_get_config_var_wd(tc, "binary128_dso", "libc.so.12");
	const char *prefix = atf_tc_get_config_var_wd(tc, "binary128_prefix", "");
	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	unsigned h;

	ATF_REQUIRE_MSG(handle != NULL, "dlopen %s: %s", path, dlerror());
	for (h = 0; h < __arraycount(helpers); h++) {
		char symbol[128];
		Dl_info provider;
		struct stat requested, bound;
		int n = snprintf(symbol, sizeof(symbol), "%s%s", prefix, helpers[h]);

		ATF_REQUIRE(n > 0 && (size_t)n < sizeof(symbol));
		functions[h] = dlsym(handle, symbol);
		ATF_REQUIRE_MSG(functions[h] != NULL, "missing %s in %s", symbol, path);
		ATF_REQUIRE(dladdr(functions[h], &provider) != 0);
		ATF_REQUIRE(provider.dli_fname != NULL);
		if (strcmp(path, "libc.so.12") == 0) {
			ATF_REQUIRE(strstr(provider.dli_fname, "libc.so") != NULL);
		} else {
			ATF_REQUIRE(stat(path, &requested) == 0);
			ATF_REQUIRE(stat(provider.dli_fname, &bound) == 0);
			ATF_REQUIRE(requested.st_dev == bound.st_dev &&
			    requested.st_ino == bound.st_ino);
		}
		printf("BIND %s address=%p provider=%s\n", symbol, functions[h],
		    provider.dli_fname);
	}
	ATF_REQUIRE_MSG(functions[6] == functions[7],
	    "__cmptf2 must retain the ELF __letf2 alias");
	return handle;
}

static void
require_format(void)
{
	const uint32_t endian = 1;
	uint64_t cr = read_fpcr(), sr = read_fpsr();
	struct result result;

	ATF_REQUIRE(LDBL_MANT_DIG == 113 && sizeof(long double) == 16);
	if (*(const char *)&endian != 1)
		atf_tc_skip("Raw q0/q1 fixture currently requires little endian");
	if (cr & TRAPS)
		atf_tc_skip("Inherited FPCR enables traps; masked matrix requires them disabled");
	comparetf2_call((void *)comparetf2_capture, operands[7].bits,
	    operands[8].bits, &result);
	write_fpcr(cr);
	write_fpsr(sr);
	ATF_REQUIRE(memcmp(comparetf2_capture_a, operands[7].bits, 16) == 0);
	ATF_REQUIRE(memcmp(comparetf2_capture_b, operands[8].bits, 16) == 0);
}

static unsigned
matrix(void **functions)
{
	uint64_t cr = read_fpcr(), sr = read_fpsr();
	uint64_t seeded = (sr & ~IOC) | UINT64_C(0x08000002); /* DZC and QC. */
	unsigned h, a, b, seed, rows = 0, failures = 0;

	for (h = 0; h < __arraycount(helpers); h++) {
		for (a = 0; a < __arraycount(operands); a++) {
			for (b = 0; b < __arraycount(operands); b++) {
				int invalid = h < 3 ? operands[a].nan == 2 ||
				    operands[b].nan == 2 : operands[a].nan || operands[b].nan;

				for (seed = 0; seed < 2; seed++) {
					struct result result;
					uint64_t initial = seeded | seed;
					int fe, ok;

					write_fpsr(initial);
					comparetf2_call(functions[h], operands[a].bits,
					    operands[b].bits, &result);
					fe = !!fetestexcept(FE_INVALID);
					ok = predicate(h, result.value, &operands[a], &operands[b]) &&
					    result.fpsr == (initial | !!invalid) &&
					    fe == !!(seed || invalid) && result.fpcr == cr;
					/* Restore before reporting a failure or calling ATF. */
					write_fpcr(cr);
					write_fpsr(sr);
					if (!ok) {
						printf("FAIL %s %s %s seed=%u value=%d "
						    "FPSR=%#" PRIx64 " FPCR=%#" PRIx64 " FE_INVALID=%d\n",
						    helpers[h], operands[a].name, operands[b].name,
						    seed, result.value, result.fpsr, result.fpcr, fe);
						failures++;
					}
					rows++;
				}
			}
		}
	}
	write_fpcr(cr);
	write_fpsr(sr);
	failures += read_fpcr() != cr || read_fpsr() != sr;
	printf("SUMMARY rows=%u failures=%u FPCR=%#" PRIx64 " FPSR=%#" PRIx64 "\n",
	    rows, failures, cr, sr);
	return failures;
}

ATF_TC(masked_matrix);
ATF_TC_HEAD(masked_matrix, tc)
{
	atf_tc_set_md_var(tc, "descr", "Raw binary128 helper predicates, INVALID and FP state");
}
ATF_TC_BODY(masked_matrix, tc)
{
	void *functions[__arraycount(helpers)], *handle;
	unsigned failures;

	require_format();
	handle = open_helpers(tc, functions);
	/* Fresh process FPCR is retained, including inherited FZ and DN. */
	failures = matrix(functions);
	(void)dlclose(handle);
	ATF_CHECK_EQ(failures, 0);
}

ATF_TC(controlled_modes);
ATF_TC_HEAD(controlled_modes, tc)
{
	atf_tc_set_md_var(tc, "descr", "Binary128 comparison under controlled rounding/FZ/DN child modes");
}
ATF_TC_BODY(controlled_modes, tc)
{
	void *functions[__arraycount(helpers)], *handle;
	uint64_t original_cr, original_sr;
	unsigned mode;

	require_format();
	handle = open_helpers(tc, functions);
	original_cr = read_fpcr();
	original_sr = read_fpsr();
	fflush(NULL);
	for (mode = 0; mode < 16; mode++) {
		pid_t child = fork();
		int status;

		ATF_REQUIRE(child >= 0);
		if (child == 0) {
			unsigned failures;
			uint64_t controlled = (original_cr & ~MODES) | ((uint64_t)mode << 22);

			write_fpcr(controlled);
			if (read_fpcr() != controlled) {
				write_fpcr(original_cr);
				write_fpsr(original_sr);
				_exit(77);
			}
			failures = matrix(functions);
			write_fpcr(original_cr);
			write_fpsr(original_sr);
			fflush(NULL);
			_exit(failures ? 1 : 0);
		}
		ATF_REQUIRE(waitpid(child, &status, 0) == child);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 77) {
			(void)dlclose(handle);
			atf_tc_skip("FPU does not implement requested rounding/FZ/DN mode %u", mode);
		}
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "mode=%u child status=%#x", mode, status);
	}
	(void)dlclose(handle);
	ATF_CHECK_EQ(read_fpcr(), original_cr);
	ATF_CHECK_EQ(read_fpsr(), original_sr);
}

struct trap_record {
	int signal, code;
};
static int trap_fd;

static void
trap_handler(int sig, siginfo_t *info, void *context)
{
	struct trap_record record = { sig, info->si_code };
	(void)context;
	(void)write(trap_fd, &record, sizeof(record));
	_exit(0);
}

ATF_TC(invalid_traps);
ATF_TC_HEAD(invalid_traps, tc)
{
	atf_tc_set_md_var(tc, "descr", "INVALID SIGFPE delivery only when hardware implements writable IOE");
	atf_tc_set_md_var(tc, "timeout", "30");
}
ATF_TC_BODY(invalid_traps, tc)
{
	void *functions[__arraycount(helpers)], *handle;
	uint64_t cr = read_fpcr(), sr = read_fpsr(), enabled;
	unsigned test;

	require_format();
	handle = open_helpers(tc, functions);
	/* Like libm FPU_EXC_PREREQ: unsupported enable bits require a skip. */
	write_fpcr((cr & ~TRAPS) | IOE);
	enabled = read_fpcr();
	write_fpcr(cr);
	write_fpsr(sr);
	if ((enabled & IOE) == 0) {
		(void)dlclose(handle);
		atf_tc_skip("FPU does not implement writable INVALID trap enable (IOE)");
	}
	fflush(NULL);
	for (test = 0; test < 3; test++) {
		int fds[2], status;
		pid_t child;
		struct trap_record record = { -1, -1 };
		ssize_t bytes;

		ATF_REQUIRE(pipe(fds) == 0);
		child = fork();
		ATF_REQUIRE(child >= 0);
		if (child == 0) {
			struct sigaction action;
			struct result result;

			(void)close(fds[0]);
			trap_fd = fds[1];
			memset(&action, 0, sizeof(action));
			action.sa_sigaction = trap_handler;
			action.sa_flags = SA_SIGINFO;
			sigemptyset(&action.sa_mask);
			if (sigaction(SIGFPE, &action, NULL) != 0)
				_exit(2);
			write_fpsr(sr & ~IOC);
			write_fpcr((cr & ~TRAPS) | IOE);
			/* sNaN quiet, qNaN ordered, then qNaN quiet/no-trap. */
			comparetf2_call(functions[test == 1 ? 6 : 0],
			    operands[test == 0 ? 8 : 7].bits, operands[4].bits, &result);
			write_fpcr(cr);
			write_fpsr(sr);
			record.signal = 0;
			record.code = 0;
			(void)write(fds[1], &record, sizeof(record));
			_exit(test == 2 && result.value != 0 ? 0 : 1);
		}
		(void)close(fds[1]);
		bytes = read(fds[0], &record, sizeof(record));
		(void)close(fds[0]);
		ATF_REQUIRE(waitpid(child, &status, 0) == child);
		printf("TRAP case=%u signal=%d si_code=%d status=%#x\n",
		    test, record.signal, record.code, status);
		ATF_CHECK(bytes == sizeof(record));
		ATF_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		ATF_CHECK_EQ(record.signal, test == 2 ? 0 : SIGFPE);
		/* AArch64 trap.c currently reports FPE_FLTUND for all FP traps.
		 * Preserve the observed si_code; do not claim INVALID classification. */
	}
	(void)dlclose(handle);
	write_fpcr(cr);
	write_fpsr(sr);
	ATF_CHECK_EQ(read_fpcr(), cr);
	ATF_CHECK_EQ(read_fpsr(), sr);
}

ATF_TC(addtf3_rounding);
ATF_TC_HEAD(addtf3_rounding, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check binary128 addition rounding and inexact flags");
}
ATF_TC_BODY(addtf3_rounding, tc)
{
	const int modes[] = { FE_TONEAREST, FE_UPWARD, FE_DOWNWARD,
	    FE_TOWARDZERO };
	volatile long double one = 1.0L, half_ulp = 0x1p-113L;
	const char *prefix = atf_tc_get_config_var_wd(tc, "binary128_prefix", "");
	void *functions[__arraycount(helpers)], *handle, *symbol;
	long double (*add)(long double, long double);
	Dl_info add_provider, compare_provider;
	struct stat add_file, compare_file;
	char name[128];
	fenv_t saved;
	size_t i;
	int n;

	handle = open_helpers(tc, functions);
	n = snprintf(name, sizeof(name), "%s__addtf3", prefix);
	ATF_REQUIRE(n > 0 && (size_t)n < sizeof(name));
	symbol = dlsym(handle, name);
	ATF_REQUIRE_MSG(symbol != NULL, "missing %s", name);
	ATF_REQUIRE(dladdr(symbol, &add_provider) != 0);
	ATF_REQUIRE(dladdr(functions[0], &compare_provider) != 0);
	ATF_REQUIRE(add_provider.dli_fname != NULL &&
	    compare_provider.dli_fname != NULL);
	ATF_REQUIRE(stat(add_provider.dli_fname, &add_file) == 0);
	ATF_REQUIRE(stat(compare_provider.dli_fname, &compare_file) == 0);
	ATF_REQUIRE(add_file.st_dev == compare_file.st_dev &&
	    add_file.st_ino == compare_file.st_ino);
	add = (long double (*)(long double, long double))symbol;
	ATF_REQUIRE(fegetenv(&saved) == 0);
	for (i = 0; i < __arraycount(modes); i++) {
		long double result;
		int raised;

		ATF_REQUIRE(fesetround(modes[i]) == 0);
		ATF_REQUIRE(fegetround() == modes[i]);
		ATF_REQUIRE(feclearexcept(FE_ALL_EXCEPT) == 0);
		result = add(one, half_ulp);
		raised = fetestexcept(FE_INEXACT);
		ATF_CHECK(result == (modes[i] == FE_UPWARD ?
		    0x1.0000000000000000000000000001p0L : 1.0L));
		ATF_CHECK(raised != 0);
		ATF_REQUIRE(feclearexcept(FE_ALL_EXCEPT) == 0);
		result = add(-one, -half_ulp);
		ATF_CHECK(result == (modes[i] == FE_DOWNWARD ?
		    -0x1.0000000000000000000000000001p0L : -1.0L));
		ATF_CHECK(fetestexcept(FE_INEXACT) != 0);
		ATF_REQUIRE(feclearexcept(FE_ALL_EXCEPT) == 0);
		result = add(one, one);
		ATF_CHECK(result == 2.0L);
		ATF_CHECK(fetestexcept(FE_INEXACT) == 0);
	}
	ATF_REQUIRE(fesetenv(&saved) == 0);
	(void)dlclose(handle);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, masked_matrix);
	ATF_TP_ADD_TC(tp, controlled_modes);
	ATF_TP_ADD_TC(tp, invalid_traps);
	ATF_TP_ADD_TC(tp, addtf3_rounding);
	return atf_no_error();
}
