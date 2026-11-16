/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: original actual-source ACPI CPU capacity contracts, AI-assisted. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The runner extracts cpu_acpi_capacity() verbatim from cpu_acpi.c.  The
 * first-class rule of arm_cpu_do_topology() (slow means below the best
 * capacity seen) is modeled here.  This does not qualify the scheduler. */
#define __arraycount(x) (sizeof(x) / sizeof((x)[0]))
#include "cpu-acpi-capacity.inc"

static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

#define A53	UINT64_C(0x410fd034)
#define A55	UINT64_C(0x412fd050)
#define A72	UINT64_C(0x410fd083)
#define A76	UINT64_C(0x414fd0b0)
#define A510	UINT64_C(0x411fd461)
#define A520	UINT64_C(0x410fd801)
#define A720	UINT64_C(0x410fd811)
#define X4	UINT64_C(0x410fd821)
#define N1	UINT64_C(0x413fd0c1)
#define ORYON	UINT64_C(0x511f0011)
#define UNKNOWN	UINT64_C(0x000000000)

struct core { uint8_t class; uint64_t midr; bool slow; };

/* Returns true when every core got the expected class. */
static bool
classes_match(const struct core *cores, size_t n)
{
    uint32_t best = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        const uint32_t cap = cpu_acpi_capacity(cores[i].class, cores[i].midr);
        if (cap > best)
            best = cap;
    }
    for (i = 0; i < n; i++) {
        const uint32_t cap = cpu_acpi_capacity(cores[i].class, cores[i].midr);
        if ((cap < best) != cores[i].slow)
            return false;
    }
    return true;
}
#define CLASSES(t) CHECK(classes_match((t), __arraycount(t)))

int
main(void)
{
    /* CIX Sky1 (Orange Pi 6 Plus), MADT order, every class zero. */
    static const struct core sky1[] = {
        {0, A720, false}, {0, A720, false},
        {0, A520, true}, {0, A520, true}, {0, A520, true}, {0, A520, true},
        {0, A720, false}, {0, A720, false}, {0, A720, false},
        {0, A720, false}, {0, A720, false}, {0, A720, false},
    };
    /* The slow cores attach first: the order must not matter. */
    static const struct core little_first[] = {
        {0, A520, true}, {0, A720, false}, {0, A520, true},
    };
    /* Symmetric systems stay symmetric, efficiency cores included. */
    static const struct core all_a53[] = { {0, A53, false}, {0, A53, false} };
    static const struct core all_n1[] = { {0, N1, false}, {0, N1, false} };
    static const struct core vm[] = { {0, UNKNOWN, false}, {0, UNKNOWN, false} };
    static const struct core all_a55_class3[] = { {3, A55, false}, {3, A55, false} };
    /* Cores that are not known efficiency cores are never told apart. */
    static const struct core big_and_prime[] = {
        {0, A720, false}, {0, X4, false}, {0, ORYON, false}, {0, UNKNOWN, false},
    };
    /* Firmware that fills the class decides alone, as before the fallback. */
    static const struct core firmware[] = {
        {0, A55, true}, {1, A76, false}, {0, A55, true}, {1, A76, false},
    };
    static const struct core three_classes[] = {
        {0, A510, true}, {1, A720, true}, {2, X4, false},
    };
    static const struct core firmware_inverted[] = {
        {1, A53, false}, {0, A72, true},
    };
    static const struct core same_kind_two_classes[] = {
        {0, A76, true}, {1, A76, false},
    };

    CLASSES(sky1);
    CLASSES(little_first);
    CLASSES(all_a53);
    CLASSES(all_n1);
    CLASSES(vm);
    CLASSES(all_a55_class3);
    CLASSES(big_and_prime);
    CLASSES(firmware);
    CLASSES(three_classes);
    CLASSES(firmware_inverted);
    CLASSES(same_kind_two_classes);

    /* The revision and variant fields do not change the kind. */
    CHECK(cpu_acpi_capacity(0, UINT64_C(0x410fd800)) ==
        cpu_acpi_capacity(0, UINT64_C(0x41ffd80f)));
    /* Another implementer with the same part number is another core. */
    CHECK(cpu_acpi_capacity(0, UINT64_C(0x510fd800)) >
        cpu_acpi_capacity(0, A520));
    /* A higher class outranks any kind in a lower class; no overflow. */
    CHECK(cpu_acpi_capacity(1, A520) > cpu_acpi_capacity(0, A720));
    CHECK(cpu_acpi_capacity(255, A720) == 511);
    /* The upper MIDR bits of the 64-bit register are ignored. */
    CHECK(cpu_acpi_capacity(0, UINT64_C(0xffffffff00000000) | A520) ==
        cpu_acpi_capacity(0, A520));

    if (failures != 0) {
        fprintf(stderr, "%u of %u checks failed\n", failures, checks);
        return 1;
    }
    printf("ACPI CPU capacity contract PASS (%u checks)\n", checks);
    return 0;
}
