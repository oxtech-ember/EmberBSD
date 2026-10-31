/* $NetBSD$ */

/*
 * The boundary between the NetBSD kernel and the ESP32-S31 radio code.
 *
 * The radio code is the vendor's libraries plus the glue built against the
 * vendor's headers (wifi/glue.c).  It sees the kernel only through the
 * espwifi_os_* services below, and the kernel sees it only through the
 * espwifi_* entry points.  Both sides include this file; it uses nothing
 * but the C integer types and the system declaration attributes.
 */

#ifndef _RISCV_ESPRESSIF_ESP_WIFI_OS_H_
#define _RISCV_ESPRESSIF_ESP_WIFI_OS_H_

#include <sys/cdefs.h>

#define	ESPWIFI_WAIT_FOREVER	0xffffffffU
#define	ESPWIFI_TICK_MS		10

/* Services of the kernel.  Times are in ticks of ESPWIFI_TICK_MS. */
void	*espwifi_os_malloc(unsigned long);
void	*espwifi_os_malloc_internal(unsigned long);
void	*espwifi_os_realloc(void *, unsigned long);
void	espwifi_os_free(void *);
unsigned long espwifi_os_free_heap(void);

unsigned int espwifi_os_critical_enter(void);
void	espwifi_os_critical_exit(unsigned int);
int	espwifi_os_in_isr(void);

void	*espwifi_os_sem_create(unsigned int, unsigned int);
void	espwifi_os_sem_delete(void *);
int	espwifi_os_sem_take(void *, unsigned int);
int	espwifi_os_sem_give(void *);
void	*espwifi_os_thread_sem(void);

void	*espwifi_os_mutex_create(void);
void	espwifi_os_mutex_delete(void *);
int	espwifi_os_mutex_lock(void *);
int	espwifi_os_mutex_unlock(void *);
void	espwifi_os_lock_acquire(void **);
void	espwifi_os_lock_release(void **);

void	*espwifi_os_queue_create(unsigned int, unsigned int);
void	espwifi_os_queue_delete(void *);
int	espwifi_os_queue_send(void *, const void *, unsigned int, int);
int	espwifi_os_queue_recv(void *, void *, unsigned int);
unsigned int espwifi_os_queue_waiting(void *);

void	*espwifi_os_event_create(void);
void	espwifi_os_event_delete(void *);
unsigned int espwifi_os_event_set(void *, unsigned int);
unsigned int espwifi_os_event_clear(void *, unsigned int);
unsigned int espwifi_os_event_wait(void *, unsigned int, int, int,
	    unsigned int);

int	espwifi_os_task_create(void (*)(void *), const char *, unsigned int,
	    void *, void **);
void	espwifi_os_task_delete(void *);
void	espwifi_os_task_delay(unsigned int);
void	*espwifi_os_task_current(void);

void	*espwifi_os_timer_create(void (*)(void *), void *);
void	espwifi_os_timer_arm(void *, unsigned int, int);
void	espwifi_os_timer_disarm(void *);
void	espwifi_os_timer_delete(void *);

void	espwifi_os_intr_establish(unsigned int, void (*)(void *), void *);
void	espwifi_os_intr_enable(unsigned int, int);

long long espwifi_os_time_us(void);
void	espwifi_os_delay_us(unsigned int);
unsigned int espwifi_os_random(void);
int	espwifi_os_secret_random(void *, unsigned long);
int	espwifi_os_ct_equal(const void *, const void *, unsigned long);
void	espwifi_os_vlog(const char *, __builtin_va_list);
void	espwifi_os_log(const char *, ...) __attribute__((format(printf, 1, 2)));
void	espwifi_os_panic(const char *) __dead;

/* Frames and state changes from the radio. */
void	espwifi_os_rx(const void *, unsigned int);
void	espwifi_os_link(int);

/* Entry points of the radio code; the caller is a thread made for it. */
int	espwifi_start(void);
int	espwifi_scan(void);
int	espwifi_seen(const char *);
int	espwifi_linked(void);
int	espwifi_connect(const char *, const char *);
int	espwifi_tx(const void *, unsigned int);
void	espwifi_get_mac(unsigned char *);

#endif /* _RISCV_ESPRESSIF_ESP_WIFI_OS_H_ */
