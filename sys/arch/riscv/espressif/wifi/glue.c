/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The radio of the ESP32-S31 under NetBSD: what the vendor's libraries and
 * the ESP-IDF sources built beside them expect from an operating system,
 * expressed through the kernel services of esp_wifi_os.h.
 *
 * This file is compiled against the ESP-IDF headers, not the kernel's.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "soc/soc_caps.h"
#include "soc/interrupts.h"
#include "esp_private/esp_modem_clock.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/wifi_os_adapter.h"
#include "esp_private/wifi.h"
#include "esp_private/phy.h"
#include "esp_phy_init.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_timer.h"
#include "esp32s31/rom/ets_sys.h"
#include "hal/efuse_ll.h"
#include "hal/clk_tree_ll.h"
#include "hal/clk_gate_ll.h"
#include "esp_private/esp_clk_tree_common.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "hal/pmu_ll.h"
#include "hal/pmu_types.h"
#include "hal/regi2c_ctrl_ll.h"
#include "esp_rom_sys.h"
#include "esp_private/regi2c_ctrl.h"
#include "soc/regi2c_bias.h"
#include "soc/regi2c_dig_reg.h"
#include "soc/pmu_reg.h"
#include "soc/hp_alive_sys_reg.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "modem/modem_syscon_reg.h"
#include "modem/modem_lpcon_reg.h"

#include "../esp_wifi_os.h"

#define	TICKS(t)	((t) == OSI_FUNCS_TIME_BLOCKING ? ESPWIFI_WAIT_FOREVER : (t))

/*
 * Small C library.  The kernel supplies memcpy and friends; these are the
 * names it does not have or spells differently.
 */

void	free(void *);
void	*malloc(size_t);
void	*calloc(size_t, size_t);
int	vsnprintf(char *, size_t, const char *, va_list);
int	sprintf(char *, const char *, ...);
int	puts(const char *);
double	floor(double);
void	abort(void);

void
free(void *p)
{
	espwifi_os_free(p);
}

void *
malloc(size_t n)
{
	return espwifi_os_malloc(n);
}

void *
calloc(size_t n, size_t size)
{
	void *p = espwifi_os_malloc(n * size);

	if (p != NULL)
		memset(p, 0, n * size);
	return p;
}

int
sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, 256, fmt, ap);
	va_end(ap);
	return n;
}

int
puts(const char *s)
{
	espwifi_os_log("%s\n", s);
	return 0;
}

void
abort(void)
{
	espwifi_os_panic("abort");
	for (;;)
		continue;
}

void __assert_func(const char *, int, const char *, const char *) __dead;

void
__assert_func(const char *file, int line, const char *func, const char *expr)
{
	espwifi_os_log("espwifi: assertion \"%s\" failed: %s:%d %s\n",
	    expr, file, line, func);
	espwifi_os_panic("assert");
	for (;;)
		continue;
}

void *realloc(void *, size_t);

void *
realloc(void *p, size_t n)
{
	return espwifi_os_realloc(p, n);
}

void vTaskDelay(uint32_t);

void
vTaskDelay(uint32_t ticks)
{
	espwifi_os_task_delay(ticks);
}

/* One processor: a critical section makes the exchange atomic. */
bool __atomic_compare_exchange_1(volatile void *, void *, uint8_t, bool,
    int, int);

bool
__atomic_compare_exchange_1(volatile void *ptr, void *expected,
    uint8_t desired, bool weak, int success, int failure)
{
	volatile uint8_t *p = ptr;
	uint8_t *e = expected;
	bool ok;

	espwifi_os_critical_enter();
	ok = *p == *e;
	if (ok)
		*p = desired;
	else
		*e = *p;
	espwifi_os_critical_exit(0);
	return ok;
}

/* Calendar time is not needed by the pre-shared key exchange. */
void *__gmtime50(const void *);
long long __mktime50(void *);

void *
__gmtime50(const void *t)
{
	return NULL;
}

long long
__mktime50(void *tm)
{
	return -1;
}

/* Access point and WPS code is not built. */
void wpa_receive(void *, void *, uint8_t *, size_t);
void *wps_get_wps_sm_cb(void);

void
wpa_receive(void *auth, void *sm, uint8_t *data, size_t len)
{
}

void *
wps_get_wps_sm_cb(void)
{
	return NULL;
}

void __assert13(const char *, int, const char *, const char *);

void
__assert13(const char *file, int line, const char *func, const char *expr)
{
	__assert_func(file, line, func, expr);
}

void
_esp_error_check_failed(esp_err_t rc, const char *file, int line,
    const char *func, const char *expr)
{
	espwifi_os_log("espwifi: error %#x from %s: %s:%d %s\n", rc, expr,
	    file, line, func);
	espwifi_os_panic("error check");
	for (;;)
		continue;
}

int espwifi_putchar(int);

int
espwifi_putchar(int c)
{
	espwifi_os_log("%c", c);
	return c;
}

char *strtok(char *, const char *);

char *
strtok(char *s, const char *delim)
{
	static char *next;
	char *start;

	if (s == NULL)
		s = next;
	if (s == NULL)
		return NULL;
	while (*s != '\0' && strchr(delim, *s) != NULL)
		s++;
	if (*s == '\0') {
		next = NULL;
		return NULL;
	}
	start = s;
	while (*s != '\0' && strchr(delim, *s) == NULL)
		s++;
	if (*s != '\0') {
		*s = '\0';
		next = s + 1;
	} else {
		next = NULL;
	}
	return start;
}

/*
 * floor() and the double to unsigned 64-bit conversion, by the bits: the
 * kernel has no floating point of its own.  A double travels in two
 * integer registers on this ABI.
 */
double
floor(double x)
{
	union { double d; uint64_t u; } v = { .d = x };
	const int e = (int)((v.u >> 52) & 0x7ff) - 1023;
	const bool neg = (v.u >> 63) != 0;

	if (e >= 52)
		return x;
	if (e < 0) {
		if ((v.u << 1) == 0)
			return x;
		v.u = neg ? 0xbff0000000000000ULL : 0;
		return v.d;
	}
	const uint64_t frac = 0x000fffffffffffffULL >> e;
	if ((v.u & frac) == 0)
		return x;
	if (neg)
		v.u += frac;
	v.u &= ~frac;
	return v.d;
}

uint64_t __fixunsdfdi(double);

uint64_t
__fixunsdfdi(double x)
{
	union { double d; uint64_t u; } v = { .d = x };
	const int e = (int)((v.u >> 52) & 0x7ff) - 1023;
	const uint64_t m = (v.u & 0x000fffffffffffffULL) | (1ULL << 52);

	if ((v.u >> 63) != 0 || e < 0)
		return 0;
	if (e > 63)
		return ~0ULL;
	return e >= 52 ? m << (e - 52) : m >> (52 - e);
}

/*
 * Logging.
 */

void
esp_log_writev(esp_log_level_t level, const char *tag, const char *fmt,
    va_list ap)
{
	espwifi_os_vlog(fmt, ap);
}

void
esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	espwifi_os_vlog(fmt, ap);
	va_end(ap);
}

void
esp_log(esp_log_config_t config, const char *tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	espwifi_os_vlog(fmt, ap);
	va_end(ap);
}

uint32_t
esp_log_timestamp(void)
{
	return (uint32_t)(espwifi_os_time_us() / 1000);
}

uint32_t
esp_log_early_timestamp(void)
{
	return esp_log_timestamp();
}

#define	BLOB_PRINTF(name)					\
int name(const char *, ...);					\
int								\
name(const char *fmt, ...)					\
{								\
	va_list ap;						\
								\
	va_start(ap, fmt);					\
	espwifi_os_vlog(fmt, ap);				\
	va_end(ap);						\
	return 0;						\
}

BLOB_PRINTF(net80211_printf)
BLOB_PRINTF(pp_printf)
BLOB_PRINTF(phy_printf)
BLOB_PRINTF(esp_rom_printf)

/*
 * Time and timers.
 */

struct esp_timer {
	void	*os;
	void	(*func)(void *);
	void	*arg;
};

int64_t
esp_timer_get_time(void)
{
	return espwifi_os_time_us();
}

esp_err_t
esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
	struct esp_timer *t = espwifi_os_malloc(sizeof(*t));

	if (t == NULL)
		return ESP_ERR_NO_MEM;
	t->func = args->callback;
	t->arg = args->arg;
	t->os = espwifi_os_timer_create(t->func, t->arg);
	if (t->os == NULL) {
		espwifi_os_free(t);
		return ESP_ERR_NO_MEM;
	}
	*out = t;
	return ESP_OK;
}

esp_err_t
esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us)
{
	espwifi_os_timer_arm(t->os, (unsigned int)us, 1);
	return ESP_OK;
}

esp_err_t
esp_timer_start_once(esp_timer_handle_t t, uint64_t us)
{
	espwifi_os_timer_arm(t->os, (unsigned int)us, 0);
	return ESP_OK;
}

esp_err_t
esp_timer_stop(esp_timer_handle_t t)
{
	espwifi_os_timer_disarm(t->os);
	return ESP_OK;
}

esp_err_t
esp_timer_delete(esp_timer_handle_t t)
{
	espwifi_os_timer_delete(t->os);
	espwifi_os_free(t);
	return ESP_OK;
}

/*
 * The libraries keep their timers in ETSTimer structures of their own and
 * do not always clear them first.  As the vendor's code does, timer_period
 * holds a mark that says timer_arg is ours.
 */
#define	ETS_TIMER_MARK	0x12121212

static void
timer_disarm_wrapper(void *ptimer)
{
	ETSTimer *t = ptimer;

	if (t->timer_period == ETS_TIMER_MARK && t->timer_arg != NULL)
		espwifi_os_timer_disarm(t->timer_arg);
}

static void
timer_done_wrapper(void *ptimer)
{
	ETSTimer *t = ptimer;

	if (t->timer_period == ETS_TIMER_MARK && t->timer_arg != NULL) {
		espwifi_os_timer_delete(t->timer_arg);
		t->timer_arg = NULL;
		t->timer_period = 0;
	}
}

static void
timer_setfn_wrapper(void *ptimer, void *func, void *arg)
{
	ETSTimer *t = ptimer;

	if (t->timer_period != ETS_TIMER_MARK)
		memset(t, 0, sizeof(*t));
	if (t->timer_arg != NULL)
		espwifi_os_timer_delete(t->timer_arg);
	t->timer_arg = espwifi_os_timer_create(func, arg);
	t->timer_period = ETS_TIMER_MARK;
}

static void
timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
	ETSTimer *t = ptimer;

	if (t->timer_period == ETS_TIMER_MARK && t->timer_arg != NULL)
		espwifi_os_timer_arm(t->timer_arg, us, repeat);
}

static void
timer_arm_wrapper(void *ptimer, uint32_t ms, bool repeat)
{
	timer_arm_us_wrapper(ptimer, ms * 1000, repeat);
}

/* Timers under the names the vendor's WPA code uses. */
void ets_timer_setfn(ETSTimer *, ETSTimerFunc *, void *);
void ets_timer_arm(ETSTimer *, uint32_t, bool);
void ets_timer_arm_us(ETSTimer *, uint32_t, bool);
void ets_timer_disarm(ETSTimer *);
void ets_timer_done(ETSTimer *);

void
ets_timer_setfn(ETSTimer *t, ETSTimerFunc *func, void *arg)
{
	timer_setfn_wrapper(t, func, arg);
}

void
ets_timer_arm(ETSTimer *t, uint32_t ms, bool repeat)
{
	timer_arm_wrapper(t, ms, repeat);
}

void
ets_timer_arm_us(ETSTimer *t, uint32_t us, bool repeat)
{
	timer_arm_us_wrapper(t, us, repeat);
}

void
ets_timer_disarm(ETSTimer *t)
{
	timer_disarm_wrapper(t);
}

void
ets_timer_done(ETSTimer *t)
{
	timer_done_wrapper(t);
}

struct newlib_timeval {
	int64_t	tv_sec;
	int32_t	tv_usec;
};

static int
get_time_wrapper(void *t)
{
	struct newlib_timeval *tv = t;
	const int64_t us = espwifi_os_time_us();

	tv->tv_sec = us / 1000000;
	tv->tv_usec = (int32_t)(us % 1000000);
	return 0;
}

/*
 * Interrupts.  The libraries route a source to a line of their choice and
 * then install a handler for the line; the kernel wants the source.
 */

#define	NLINES	32
#define	NSOURCES	4

static struct {
	uint32_t	src[NSOURCES];
	unsigned	nsrc;
	bool		established;
} lines[NLINES];

static void
set_intr_wrapper(int32_t cpu, uint32_t src, uint32_t num, int32_t prio)
{
	if (num >= NLINES || lines[num].nsrc == NSOURCES) {
		espwifi_os_log("espwifi: cannot route source %u to line %u\n",
		    (unsigned)src, (unsigned)num);
		return;
	}
	for (unsigned i = 0; i < lines[num].nsrc; i++) {
		if (lines[num].src[i] == src)
			return;
	}
	lines[num].src[lines[num].nsrc++] = src;
}

static void
clear_intr_wrapper(uint32_t src, uint32_t num)
{
}

/* Several sources may share a line; the handler then serves them all. */
static void
set_isr_wrapper(int32_t n, void *f, void *arg)
{
	if (n < 0 || n >= NLINES || lines[n].nsrc == 0) {
		espwifi_os_log("espwifi: handler for unrouted line %d\n",
		    (int)n);
		return;
	}
	for (unsigned i = 0; i < lines[n].nsrc; i++)
		espwifi_os_intr_establish(lines[n].src[i], f, arg);
	lines[n].established = true;
}

static void
ints_enable(uint32_t mask, int on)
{
	for (int n = 0; n < NLINES; n++) {
		if ((mask & (1U << n)) == 0 || !lines[n].established)
			continue;
		for (unsigned i = 0; i < lines[n].nsrc; i++)
			espwifi_os_intr_enable(lines[n].src[i], on);
	}
}

static void
ints_on_wrapper(uint32_t mask)
{
	ints_enable(mask, 1);
}

static void
ints_off_wrapper(uint32_t mask)
{
	ints_enable(mask, 0);
}

static bool
is_from_isr_wrapper(void)
{
	return espwifi_os_in_isr() != 0;
}

static bool
env_is_chip_wrapper(void)
{
	return true;
}

static void *
spin_lock_create_wrapper(void)
{
	static int lock;

	return &lock;
}

static void
spin_lock_delete_wrapper(void *lock)
{
}

static uint32_t
wifi_int_disable_wrapper(void *mux)
{
	return espwifi_os_critical_enter();
}

static void
wifi_int_restore_wrapper(void *mux, uint32_t tmp)
{
	espwifi_os_critical_exit(tmp);
}

static void
empty_wrapper(void)
{
}

/*
 * Semaphores, mutexes, queues, event groups, tasks.
 */

static void *
semphr_create_wrapper(uint32_t max, uint32_t init)
{
	return espwifi_os_sem_create(max, init);
}

static int32_t
semphr_take_wrapper(void *sem, uint32_t ticks)
{
	return espwifi_os_sem_take(sem, TICKS(ticks));
}

static int32_t
semphr_give_wrapper(void *sem)
{
	return espwifi_os_sem_give(sem);
}

static void *
recursive_mutex_create_wrapper(void)
{
	return espwifi_os_mutex_create();
}

static int32_t
mutex_lock_wrapper(void *m)
{
	return espwifi_os_mutex_lock(m);
}

static int32_t
mutex_unlock_wrapper(void *m)
{
	return espwifi_os_mutex_unlock(m);
}

static void *
queue_create_wrapper(uint32_t len, uint32_t size)
{
	return espwifi_os_queue_create(len, size);
}

static int32_t
queue_send_wrapper(void *q, void *item, uint32_t ticks)
{
	return espwifi_os_queue_send(q, item, TICKS(ticks), 0);
}

static int32_t
queue_send_from_isr_wrapper(void *q, void *item, void *hptw)
{
	if (hptw != NULL)
		*(int *)hptw = 0;
	return espwifi_os_queue_send(q, item, 0, 0);
}

static int32_t
queue_send_to_front_wrapper(void *q, void *item, uint32_t ticks)
{
	return espwifi_os_queue_send(q, item, TICKS(ticks), 1);
}

static int32_t
queue_recv_wrapper(void *q, void *item, uint32_t ticks)
{
	return espwifi_os_queue_recv(q, item, TICKS(ticks));
}

static uint32_t
event_group_wait_bits_wrapper(void *ev, uint32_t bits, int clear, int all,
    uint32_t ticks)
{
	return espwifi_os_event_wait(ev, bits, clear, all, TICKS(ticks));
}

/* A queue as the libraries hold it: they reach for the handle themselves. */
struct wifi_static_queue {
	void	*handle;
	void	*storage;
};

static void *
wifi_create_queue_wrapper(int len, int size)
{
	struct wifi_static_queue *q = espwifi_os_malloc(sizeof(*q));

	if (q == NULL)
		return NULL;
	q->storage = NULL;
	q->handle = espwifi_os_queue_create(len, size);
	if (q->handle == NULL) {
		espwifi_os_free(q);
		return NULL;
	}
	return q;
}

static void
wifi_delete_queue_wrapper(void *queue)
{
	struct wifi_static_queue *q = queue;

	if (q != NULL) {
		espwifi_os_queue_delete(q->handle);
		espwifi_os_free(q);
	}
}

static int32_t
task_create_wrapper(void *func, const char *name, uint32_t stack, void *param,
    uint32_t prio, void *handle)
{
	return espwifi_os_task_create(func, name, stack, param, handle);
}

static int32_t
task_create_pinned_wrapper(void *func, const char *name, uint32_t stack,
    void *param, uint32_t prio, void *handle, uint32_t core)
{
	return espwifi_os_task_create(func, name, stack, param, handle);
}

static int32_t
task_ms_to_tick_wrapper(uint32_t ms)
{
	return (int32_t)(ms / ESPWIFI_TICK_MS);
}

static int32_t
task_get_max_priority_wrapper(void)
{
	return 25;
}

/*
 * Memory.  The radio moves frames by DMA, which bypasses the cache in front
 * of the external RAM, so everything the libraries allocate comes from the
 * internal RAM first.
 */

static void *
zalloc_wrapper(size_t size)
{
	void *p = espwifi_os_malloc_internal(size);

	if (p != NULL)
		memset(p, 0, size);
	return p;
}

static void *
calloc_wrapper(size_t n, size_t size)
{
	return zalloc_wrapper(n * size);
}

static void *
malloc_wrapper(size_t size)
{
	return espwifi_os_malloc_internal(size);
}

static void *
realloc_wrapper(void *p, size_t size)
{
	return espwifi_os_realloc(p, size);
}

static uint32_t
get_free_heap_size_wrapper(void)
{
	return espwifi_os_free_heap();
}

/* What the vendor's WPA code asks of an operating system. */
struct os_time {
	uint64_t sec;
	int32_t	usec;
};

int os_get_time(struct os_time *);
unsigned long os_random(void);
int os_get_random(unsigned char *, size_t);
void os_sleep(uint64_t, uint64_t);
void forced_memzero(void *, size_t);

int
os_get_time(struct os_time *t)
{
	const int64_t us = espwifi_os_time_us();

	t->sec = (uint64_t)(us / 1000000);
	t->usec = (int32_t)(us % 1000000);
	return 0;
}

unsigned long
os_random(void)
{
	return espwifi_os_random();
}

int
os_get_random(unsigned char *buf, size_t len)
{
	return espwifi_os_secret_random(buf, len);
}

void
os_sleep(uint64_t sec, uint64_t usec)
{
	espwifi_os_task_delay((unsigned)(sec * 1000 + usec / 1000) /
	    ESPWIFI_TICK_MS);
}

#ifndef ESPWIFI_COMMON_CRYPTO
void
forced_memzero(void *p, size_t len)
{
	volatile unsigned char *v = p;

	while (len-- > 0)
		*v++ = 0;
}
#else
int
os_memcmp_const(const void *a, const void *b, size_t len)
{
	return espwifi_os_ct_equal(a, b, len) ? 0 : 1;
}
#endif

/*
 * Storage: none.  The libraries are told so at start and fall back to
 * their defaults.
 */

#define	NVS_NOT_FOUND	0x1102

static int
nvs_fail(void)
{
	return NVS_NOT_FOUND;
}

/*
 * Random numbers.
 */

static uint32_t
rand_wrapper(void)
{
	return espwifi_os_random();
}

static unsigned long
random_wrapper(void)
{
	return espwifi_os_random();
}

static int
get_random_wrapper(uint8_t *buf, size_t len)
{
	return espwifi_os_secret_random(buf, len);
}

uint32_t
esp_random(void)
{
	return espwifi_os_random();
}

/*
 * Addresses.  The station takes the base address from the eFuse block.
 */

static void
efuse_mac(uint8_t *mac)
{
	const uint32_t lo = efuse_ll_get_mac0();
	const uint32_t hi = efuse_ll_get_mac1();

	mac[0] = (uint8_t)(hi >> 8);
	mac[1] = (uint8_t)hi;
	mac[2] = (uint8_t)(lo >> 24);
	mac[3] = (uint8_t)(lo >> 16);
	mac[4] = (uint8_t)(lo >> 8);
	mac[5] = (uint8_t)lo;
}

esp_err_t
esp_efuse_mac_get_default(uint8_t *mac)
{
	efuse_mac(mac);
	return ESP_OK;
}

static int
read_mac_wrapper(uint8_t *mac, unsigned int type)
{
	efuse_mac(mac);
	if (type == 1) {	/* soft access point */
		mac[5] += 1;
	}
	return ESP_OK;
}

void
espwifi_get_mac(unsigned char *mac)
{
	efuse_mac(mac);
}

/*
 * Coexistence with Bluetooth: there is none.
 */

static int
coex_zero(void)
{
	return 0;
}

/*
 * The priority of a Wi-Fi event in the radio arbiter.  The libraries use
 * the byte whatever the function returns, and a radio left at priority 0
 * hears nothing.  The values are those of Espressif's own arbiter with
 * Wi-Fi alone on the air.
 */
static int
coex_pti_get_wrapper(uint32_t event, uint8_t *pti)
{
	if (pti == NULL)
		return 0;
	switch (event) {
	case 0:
		*pti = 10;
		break;
	case 1:
		*pti = 5;
		break;
	case 3:
		*pti = 7;
		break;
	case 10:
		*pti = 3;
		break;
	default:
		*pti = 1;
		break;
	}
	return 0;
}

static uint8_t
coex_one(void)
{
	return 1;
}

static void *
coex_null(void)
{
	return NULL;
}

static int32_t
retention_wrapper(void)
{
	return 1;
}

static bool
false_wrapper(void)
{
	return false;
}

/*
 * The period of the slow clock in microseconds, 19 fractional bits: the
 * nominal 136 kHz of the internal oscillator.
 */
static uint32_t
slowclk_cal_get_wrapper(void)
{
	return (uint32_t)((1000000ULL << 19) / 136000);
}

/*
 * The radio and its clocks.
 */

extern void set_bb_wdg(bool, bool, uint16_t, uint16_t, bool, bool, bool);
extern void phy_wifi_enable_set(uint8_t);
extern int register_chipv7_phy(const esp_phy_init_data_t *,
    esp_phy_calibration_data_t *, esp_phy_calibration_mode_t);
extern char *get_phy_version_str(void);
extern void phy_init_param_set(uint8_t);
extern void phy_wakeup_init(void);
extern const esp_phy_init_data_t phy_init_data;

/*
 * The modem takes its fast clock from the 480 MHz PLL through the 160 MHz
 * tap.  The boot loaders leave the PLL running for the USB serial port.
 */
esp_err_t
esp_clk_tree_manage_src(soc_module_clk_t clk, bool acquire)
{
	if (clk == SOC_MOD_CLK_PLL_F160M && acquire) {
		clk_ll_bbpll_enable();
		_clk_gate_ll_ref_160m_clk_en(true);
	}
	return ESP_OK;
}

void
esp_phy_common_clock_enable(void)
{
	wifi_bt_common_module_enable();
}

void
esp_phy_common_clock_disable(void)
{
	wifi_bt_common_module_disable();
}

uint32_t rtc_clk_xtal_freq_get(void);

uint32_t
rtc_clk_xtal_freq_get(void)
{
	return CONFIG_XTAL_FREQ;
}

esp_err_t esp_sleep_pd_config(int, int);

esp_err_t
esp_sleep_pd_config(int domain, int option)
{
	return ESP_OK;
}

_lock_t
phy_get_lock(void)
{
	return NULL;
}

/* The antenna is fixed; nothing switches it. */
bool esp_gpio_is_reserved(uint64_t);

bool
esp_gpio_is_reserved(uint64_t mask)
{
	return false;
}

esp_err_t
gpio_config(const gpio_config_t *cfg)
{
	return ESP_OK;
}

/* Mesh networking is not built; the libraries still name its hooks. */
#define	MESH_STUB(name)						\
int name(void);							\
int								\
name(void)							\
{								\
	espwifi_os_log("espwifi: " #name " called\n");		\
	return -1;						\
}

MESH_STUB(ieee80211_init_mesh_assoc_ie)
MESH_STUB(ieee80211_vnd_mesh_quick_get)
MESH_STUB(ieee80211_vnd_mesh_quick_set)
MESH_STUB(ieee80211_vnd_mesh_roots_get)
MESH_STUB(ieee80211_vnd_mesh_roots_set)
MESH_STUB(mesh_clear_parent_candidate)
MESH_STUB(mesh_get_parent_candidate)
MESH_STUB(mesh_get_parent_monitor_config)
MESH_STUB(mesh_get_rssi_threshold)
MESH_STUB(mesh_set_ie_crypto_config)
MESH_STUB(mesh_set_parent_candidate)
MESH_STUB(mesh_set_parent_monitor_config)
MESH_STUB(mesh_set_rssi_threshold)
MESH_STUB(mt_get_peer_info)
void *g_mt;

static bool phy_calibrated;

void
esp_phy_enable(esp_phy_modem_t modem)
{
	static void *lock;

	espwifi_os_lock_acquire(&lock);
	if (phy_get_modem_flag() == 0) {
		esp_phy_common_clock_enable();
		phy_module_enable();
		if (!phy_calibrated) {
			esp_phy_calibration_data_t *cal =
			    calloc(1, sizeof(*cal));

			if (cal == NULL)
				espwifi_os_panic("no memory for calibration");
			espwifi_os_log("espwifi: phy %s\n",
			    get_phy_version_str());
			phy_init_param_set(1);
			efuse_mac(cal->mac);
			const int rc = register_chipv7_phy(&phy_init_data,
			    cal, PHY_RF_CAL_FULL);
			if (rc != 0)
				espwifi_os_log("espwifi: calibration: %d\n",
				    rc);
			free(cal);
			phy_calibrated = true;
		} else {
			phy_wakeup_init();
		}
		phy_track_pll_init();
		phy_module_disable();
	}
	phy_set_modem_flag(modem);
	phy_track_pll();
	espwifi_os_lock_release(&lock);
}

void
esp_phy_disable(esp_phy_modem_t modem)
{
	/* The radio stays on: nothing here saves power yet. */
}

static void
phy_enable_wrapper(void)
{
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1);
	set_bb_wdg(true, false, 0x18, 0xaa, false, false, false);
}

static void
phy_disable_wrapper(void)
{
}

static int
phy_update_country_info_wrapper(const char *country)
{
	return ESP_OK;
}

static void
wifi_reset_mac_wrapper(void)
{
	modem_clock_module_mac_reset(PERIPH_WIFI_MODULE);
}

static void
wifi_clock_enable_wrapper(void)
{
	wifi_module_enable();
}

static void
wifi_clock_disable_wrapper(void)
{
	wifi_module_disable();
}

static void
regdma_link_set_write_wait_content_wrapper(void *link, uint32_t value,
    uint32_t mask)
{
}

static void *
sleep_retention_find_link_by_id_wrapper(int id)
{
	return NULL;
}

/*
 * Events.
 */

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);

/* IDF's WPA callback returns 1 for a consumed EAPOL-Key, 0 for other
 * 802.1X packets, and -1 on failure. */
int espwifi_wpa_rx_eapol(uint8_t *, uint8_t *, uint32_t);
int
espwifi_wpa_rx_eapol(uint8_t *source, uint8_t *buffer, uint32_t len)
{
	return espwifi_os_rx_eapol(source, buffer, len);
}

static void *link_change;
static int link_up;
static uint32_t stop_events, start_events;

static int32_t
event_post_wrapper(const char *base, int32_t id, void *data, size_t size,
    uint32_t ticks)
{
	switch (id) {
	case WIFI_EVENT_SCAN_DONE:
		break;
	case WIFI_EVENT_STA_START: {
		link_up = 0;
		unsigned critical;

		espwifi_os_radio_started();
		critical = espwifi_os_critical_enter();
		start_events++;
		espwifi_os_critical_exit(critical);
		espwifi_os_sem_give(link_change);
		break;
	}
	case WIFI_EVENT_STA_STOP: {
		unsigned critical;

		link_up = 0;
		espwifi_os_radio_stopped();
		critical = espwifi_os_critical_enter();
		stop_events++;
		espwifi_os_critical_exit(critical);
		espwifi_os_sem_give(link_change);
		break;
	}
	case WIFI_EVENT_STA_CONNECTED:
		if (data == NULL)
			break;
		link_up = 1;
		espwifi_os_sem_give(link_change);
		espwifi_os_link(1, ((const wifi_event_sta_connected_t *)data)->bssid);
		break;
	case WIFI_EVENT_STA_DISCONNECTED: {
		const wifi_event_sta_disconnected_t *d = data;

		espwifi_os_log("espwifi: disconnected, reason %d\n",
		    d != NULL ? d->reason : -1);
		link_up = 0;
		espwifi_os_sem_give(link_change);
		espwifi_os_link(0, d != NULL ? d->bssid : NULL);
		break;
	}
	default:
		break;
	}
	return ESP_OK;
}

/*
 * The table the libraries call through.
 */

wifi_osi_funcs_t g_wifi_osi_funcs = {
	._version = ESP_WIFI_OS_ADAPTER_VERSION,
	._env_is_chip = env_is_chip_wrapper,
	._set_intr = set_intr_wrapper,
	._clear_intr = clear_intr_wrapper,
	._set_isr = set_isr_wrapper,
	._ints_on = ints_on_wrapper,
	._ints_off = ints_off_wrapper,
	._is_from_isr = is_from_isr_wrapper,
	._spin_lock_create = spin_lock_create_wrapper,
	._spin_lock_delete = spin_lock_delete_wrapper,
	._wifi_int_disable = wifi_int_disable_wrapper,
	._wifi_int_restore = wifi_int_restore_wrapper,
	._task_yield_from_isr = empty_wrapper,
	._semphr_create = semphr_create_wrapper,
	._semphr_delete = espwifi_os_sem_delete,
	._semphr_take = semphr_take_wrapper,
	._semphr_give = semphr_give_wrapper,
	._wifi_thread_semphr_get = espwifi_os_thread_sem,
	._mutex_create = recursive_mutex_create_wrapper,
	._recursive_mutex_create = recursive_mutex_create_wrapper,
	._mutex_delete = espwifi_os_mutex_delete,
	._mutex_lock = mutex_lock_wrapper,
	._mutex_unlock = mutex_unlock_wrapper,
	._queue_create = queue_create_wrapper,
	._queue_delete = espwifi_os_queue_delete,
	._queue_send = queue_send_wrapper,
	._queue_send_from_isr = queue_send_from_isr_wrapper,
	._queue_send_to_back = queue_send_wrapper,
	._queue_send_to_front = queue_send_to_front_wrapper,
	._queue_recv = queue_recv_wrapper,
	._queue_msg_waiting = (void *)espwifi_os_queue_waiting,
	._event_group_create = espwifi_os_event_create,
	._event_group_delete = espwifi_os_event_delete,
	._event_group_set_bits = (void *)espwifi_os_event_set,
	._event_group_clear_bits = (void *)espwifi_os_event_clear,
	._event_group_wait_bits = event_group_wait_bits_wrapper,
	._task_create_pinned_to_core = task_create_pinned_wrapper,
	._task_create = task_create_wrapper,
	._task_delete = espwifi_os_task_delete,
	._task_delay = (void *)espwifi_os_task_delay,
	._task_ms_to_tick = task_ms_to_tick_wrapper,
	._task_get_current_task = espwifi_os_task_current,
	._task_get_max_priority = task_get_max_priority_wrapper,
	._malloc = malloc_wrapper,
	._free = espwifi_os_free,
	._event_post = event_post_wrapper,
	._get_free_heap_size = get_free_heap_size_wrapper,
	._rand = rand_wrapper,
	._dport_access_stall_other_cpu_start_wrap = empty_wrapper,
	._dport_access_stall_other_cpu_end_wrap = empty_wrapper,
	._wifi_pm_sleep_lock_acquire = empty_wrapper,
	._wifi_pm_sleep_lock_release = empty_wrapper,
	._phy_disable = phy_disable_wrapper,
	._phy_enable = phy_enable_wrapper,
	._phy_update_country_info = phy_update_country_info_wrapper,
	._read_mac = read_mac_wrapper,
	._timer_arm = timer_arm_wrapper,
	._timer_disarm = timer_disarm_wrapper,
	._timer_done = timer_done_wrapper,
	._timer_setfn = timer_setfn_wrapper,
	._timer_arm_us = timer_arm_us_wrapper,
	._wifi_reset_mac = wifi_reset_mac_wrapper,
	._wifi_clock_enable = wifi_clock_enable_wrapper,
	._wifi_clock_disable = wifi_clock_disable_wrapper,
	._wifi_rtc_enable_iso = empty_wrapper,
	._wifi_rtc_disable_iso = empty_wrapper,
	._esp_timer_get_time = esp_timer_get_time,
	._nvs_set_i8 = (void *)nvs_fail,
	._nvs_get_i8 = (void *)nvs_fail,
	._nvs_set_u8 = (void *)nvs_fail,
	._nvs_get_u8 = (void *)nvs_fail,
	._nvs_set_u16 = (void *)nvs_fail,
	._nvs_get_u16 = (void *)nvs_fail,
	._nvs_open = (void *)nvs_fail,
	._nvs_close = (void *)empty_wrapper,
	._nvs_commit = (void *)nvs_fail,
	._nvs_set_blob = (void *)nvs_fail,
	._nvs_get_blob = (void *)nvs_fail,
	._nvs_erase_key = (void *)nvs_fail,
	._get_random = get_random_wrapper,
	._get_time = get_time_wrapper,
	._random = random_wrapper,
	._slowclk_cal_get = slowclk_cal_get_wrapper,
	._log_write = (void *)esp_log_write,
	._log_writev = (void *)esp_log_writev,
	._log_timestamp = esp_log_timestamp,
	._malloc_internal = malloc_wrapper,
	._realloc_internal = realloc_wrapper,
	._calloc_internal = calloc_wrapper,
	._zalloc_internal = zalloc_wrapper,
	._wifi_malloc = malloc_wrapper,
	._wifi_realloc = realloc_wrapper,
	._wifi_calloc = calloc_wrapper,
	._wifi_zalloc = zalloc_wrapper,
	._wifi_create_queue = wifi_create_queue_wrapper,
	._wifi_delete_queue = wifi_delete_queue_wrapper,
	._coex_init = coex_zero,
	._coex_deinit = empty_wrapper,
	._coex_enable = coex_zero,
	._coex_disable = empty_wrapper,
	._coex_status_get = (void *)coex_zero,
	._coex_condition_set = (void *)empty_wrapper,
	._coex_wifi_request = (void *)coex_zero,
	._coex_wifi_release = (void *)coex_zero,
	._coex_wifi_channel_set = (void *)coex_zero,
	._coex_event_duration_get = (void *)coex_zero,
	._coex_pti_get = coex_pti_get_wrapper,
	._coex_schm_status_bit_clear = (void *)empty_wrapper,
	._coex_schm_status_bit_set = (void *)empty_wrapper,
	._coex_schm_interval_set = (void *)coex_zero,
	._coex_schm_interval_get = (void *)coex_zero,
	._coex_schm_curr_period_get = coex_one,
	._coex_schm_curr_phase_get = coex_null,
	._coex_schm_process_restart = coex_zero,
	._coex_schm_register_cb = (void *)coex_zero,
	._coex_register_start_cb = (void *)coex_zero,
	._regdma_link_set_write_wait_content =
	    regdma_link_set_write_wait_content_wrapper,
	._sleep_retention_find_link_by_id =
	    sleep_retention_find_link_by_id_wrapper,
	._coex_schm_flexible_period_set = (void *)coex_zero,
	._coex_schm_flexible_period_get = coex_one,
	._coex_schm_get_phase_by_idx = (void *)coex_null,
	._coex_configure_preemption_end_cb = (void *)coex_zero,
	._wifi_disable_ac_ax = false_wrapper,
	._wifi_bb_sleep_retention_attach = retention_wrapper,
	._wifi_bb_sleep_retention_detach = retention_wrapper,
	._wifi_mac_sleep_retention_attach = retention_wrapper,
	._wifi_mac_sleep_retention_detach = retention_wrapper,
	._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};

#ifndef ESPWIFI_EXTERNAL_WPA2
/* The table of ciphers and hashes comes with the vendor's WPA code. */
extern const wpa_crypto_funcs_t g_wifi_default_wpa_crypto_funcs;
int esp_supplicant_init(void);
#endif

/* Names the libraries want and this configuration leaves empty. */
uint8_t g_espnow_user_oui[3];
uint32_t mesh_sta_auth_expire_time;

/*
 * Entry points for the kernel.
 */

static esp_err_t
rx_frame(void *buffer, uint16_t len, void *eb)
{
	espwifi_os_rx(buffer, len);
	esp_wifi_internal_free_rx_buffer(eb);
	return ESP_OK;
}

void espwifi_trace(const char *);

/* The target of mkblob's TRACE option. */
void
espwifi_trace(const char *name)
{
	espwifi_os_log("espwifi: -> %s\n", name);
}

static int
espwifi_start_impl(void)
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_err_t error;

	/*
	 * The modem's clocks are gated by a code the power unit holds for
	 * each of its states.  The vendor's startup sets the code of the
	 * active state; the boot loaders of this board leave it at "sleep",
	 * which stops every modem clock.
	 */
	pmu_ll_hp_set_icg_modem(&PMU, PMU_MODE_HP_ACTIVE,
	    PMU_HP_ICG_MODEM_CODE_ACTIVE);
	pmu_ll_imm_update_dig_icg_modem_code(&PMU, true);
	pmu_ll_imm_update_dig_icg_switch(&PMU, true);

	/*
	 * The ROM times its delays by the cycle counter and still believes
	 * the processor runs from the crystal.  Measure and tell it.
	 */
	{
		const long long t0 = espwifi_os_time_us();
		uint32_t c0, c1;

		__asm__ volatile("rdcycle %0" : "=r"(c0));
		while (espwifi_os_time_us() - t0 < 4000)
			continue;
		__asm__ volatile("rdcycle %0" : "=r"(c1));
		esp_rom_set_cpu_ticks_per_us((c1 - c0 + 2000) / 4000);
	}

	/*
	 * The analog bias of the active state is off after the boot loaders
	 * of this board; the radio's analog part does not work without it.
	 */
	REG_SET_BIT(PMU_HP_ACTIVE_BIAS_REG, PMU_HP_ACTIVE_XPD_BIAS);

	/*
	 * The vendor's boot loader trims the 1.1 V reference of the analog
	 * part; the boot loaders of this board do not.
	 */
	REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 12);
	REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 12);

	/* Power the analog register bus, as the vendor's startup does. */
	regi2c_ctrl_ll_i2c_sar_periph_enable();

	/*
	 * The modem clocks are left to their gates.  Forcing them all on
	 * spoils the transmitter: the access point stops answering.
	 */
	link_change = espwifi_os_sem_create(1, 0);

	modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,
	    MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0);

	esp_wifi_internal_set_log_level(WIFI_LOG_WARNING);
	error = esp_wifi_init_internal(&cfg);
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: init: error %#x\n", error);
		return error;
	}
#ifdef ESPWIFI_EXTERNAL_WPA2
	{
		extern int espwifi_wpa2_register(void);
		if (espwifi_wpa2_register() != 0) {
			espwifi_os_log("espwifi: external WPA2 callback setup failed\n");
			return ESP_FAIL;
		}
	}
#else
	error = esp_supplicant_init();
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: supplicant: error %#x\n", error);
		return error;
	}
#endif
	error = esp_wifi_set_mode(WIFI_MODE_STA);
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: station mode: error %#x\n", error);
		return error;
	}
	error = esp_wifi_start();
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: start: error %#x\n", error);
		return error;
	}
	/*
	 * No power saving: a sleeping station wakes for beacons by a clock
	 * this port does not keep to the microsecond, and misses its frames.
	 */
	esp_wifi_set_ps(WIFI_PS_NONE);
	esp_wifi_internal_reg_rxcb(WIFI_IF_STA, rx_frame);
	return 0;
}

int
espwifi_start(void)
{
#ifdef ESPWIFI_EXTERNAL_WPA2
	return ESP_FAIL;
#else
	return espwifi_start_impl();
#endif
}

int
espwifi_start_wpa2(void)
{
#ifdef ESPWIFI_EXTERNAL_WPA2
	return espwifi_start_impl();
#else
	return ESP_FAIL;
#endif
}

static wifi_ap_record_t aps[16];
static uint16_t naps;

int
espwifi_linked(void)
{
	return link_up;
}

/* Was the network in the last scan? */
int
espwifi_seen(const char *ssid)
{
	for (unsigned i = 0; i < naps; i++) {
		if (strcmp((const char *)aps[i].ssid, ssid) == 0)
			return 1;
	}
	return 0;
}

int
espwifi_scan(void)
{
	uint16_t n = 16;
	esp_err_t error;

	naps = 0;

	/* The vendor's blocking API has its own timeout and result status. */
	error = esp_wifi_scan_start(NULL, true);
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: scan: error %#x\n", error);
		(void)esp_wifi_clear_ap_list();
		return -1;
	}
	error = esp_wifi_scan_get_ap_records(&n, aps);
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: scan results: error %#x\n", error);
		(void)esp_wifi_clear_ap_list();
		return -1;
	}
	if (n > sizeof(aps) / sizeof(aps[0])) {
		espwifi_os_log("espwifi: scan returned too many records\n");
		return -1;
	}
	naps = n;
	espwifi_os_log("espwifi: %u networks, %lu bytes of internal RAM free\n",
	    n, espwifi_os_free_heap());
	for (unsigned i = 0; i < n; i++) {
		espwifi_os_log("espwifi:  %-32s ch %2u rssi %d auth %d\n",
		    (const char *)aps[i].ssid, aps[i].primary, aps[i].rssi,
		    aps[i].authmode);
	}
	return n;
}

static void
espwifi_scan_capture_rx(void *buffer, wifi_promiscuous_pkt_type_t type)
{
	const wifi_promiscuous_pkt_t * const packet = buffer;
	const uint8_t *frame;
	unsigned len, subtype;

	if (type != WIFI_PKT_MGMT || packet == NULL)
		return;
	len = packet->rx_ctrl.sig_len;
	/* ESP-IDF reports the 802.11 FCS as part of sig_len. */
	if (len < 28)
		return;
	len -= 4;
	frame = packet->payload;
	if ((frame[0] & 0x0c) != 0)
		return;
	subtype = frame[0] & 0xf0;
	if (subtype != 0x80 && subtype != 0x50)
		return;
	espwifi_os_scan_frame(frame, len, packet->rx_ctrl.channel,
	    packet->rx_ctrl.rssi, packet->rx_ctrl.timestamp);
}

int
espwifi_scan_capture(int enable)
{
	wifi_promiscuous_filter_t filter = {
		.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
	};
	esp_err_t error;

	if (!enable)
		return esp_wifi_set_promiscuous(false);
	error = esp_wifi_set_promiscuous_rx_cb(espwifi_scan_capture_rx);
	if (error != ESP_OK)
		return error;
	error = esp_wifi_set_promiscuous_filter(&filter);
	if (error != ESP_OK)
		return error;
	return esp_wifi_set_promiscuous(true);
}

esp_err_t
esp_wifi_connect(void)
{
	return esp_wifi_connect_internal();
}

esp_err_t
esp_wifi_disconnect(void)
{
	return esp_wifi_disconnect_internal();
}

int
espwifi_disconnect(void)
{
	uint32_t stop_target, start_target;
	unsigned critical;
	long long deadline;
	esp_err_t error;

	critical = espwifi_os_critical_enter();
	stop_target = stop_events + 1;
	espwifi_os_critical_exit(critical);
	deadline = espwifi_os_time_us() + 2000000;
	error = esp_wifi_stop();
	if (error != ESP_OK)
		return error;
	/* Require STA_STOP before restart. The public API does not promise an event
	 * queue drain, so the station adapter permanently fences joins after STOP. */
	for (;;) {
		bool stopped;

		critical = espwifi_os_critical_enter();
		stopped = stop_events == stop_target;
		espwifi_os_critical_exit(critical);
		if (stopped)
			break;
		if (espwifi_os_time_us() >= deadline)
			return -1;
		(void)espwifi_os_sem_take(link_change, 10);
	}
	critical = espwifi_os_critical_enter();
	start_target = start_events + 1;
	espwifi_os_critical_exit(critical);
	error = esp_wifi_start();
	if (error != ESP_OK)
		return error;
	deadline = espwifi_os_time_us() + 2000000;
	/* A STOP tears down the vendor STA control block. Do not release the
	 * adapter's join fence until STA_START arrives after this call. */
	for (;;) {
		bool started;

		critical = espwifi_os_critical_enter();
		started = start_events == start_target;
		espwifi_os_critical_exit(critical);
		if (started)
			return 0;
		if (espwifi_os_time_us() >= deadline)
			return -1;
		(void)espwifi_os_sem_take(link_change, 10);
	}
}

int
espwifi_connect(const char *ssid, const char *psk)
{
	static wifi_config_t cfg;
	esp_err_t error;

	memset(&cfg, 0, sizeof(cfg));
	strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
	if (psk != NULL && psk[0] != '\0') {
		/* A raw key is 64 hex digits and fills the field. */
		strncpy((char *)cfg.sta.password, psk,
		    sizeof(cfg.sta.password));
		cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}
	error = esp_wifi_set_config(WIFI_IF_STA, &cfg);
	if (error == ESP_OK)
		error = esp_wifi_connect_internal();
	if (error != ESP_OK) {
		espwifi_os_log("espwifi: connect: error %#x\n", error);
		return error;
	}
	if (!espwifi_os_sem_take(link_change, 2000) || !link_up)
		return -1;
	return 0;
}

int
espwifi_tx(const void *buf, unsigned int len)
{
	return esp_wifi_internal_tx(WIFI_IF_STA, (void *)(uintptr_t)buf,
	    (uint16_t)len);
}
