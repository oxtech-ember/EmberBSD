/* SPDX-License-Identifier: BSD-2-Clause */
/* Host service model for the native NetBSD atomic API used by actual RFK.
 * User-space compiler atomics model single-copy native operations/barriers;
 * they do not qualify target helper names or kernel-link availability. */
#ifndef RTW88_RFK_ATOMIC_MODEL_H
#define RTW88_RFK_ATOMIC_MODEL_H
#include <stdint.h>
/* Observation only: lets the contract model an external context losing its
 * baseline at a real yield. Actual baseline-free producer is tested apart. */
extern volatile unsigned *rtw88_test_rfk_atomic_word;
#define atomic_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
static inline void membar_sync(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline unsigned atomic_cas_uint(volatile unsigned *p,unsigned old,unsigned value)
{ rtw88_test_rfk_atomic_word=p;
  (void)__atomic_compare_exchange_n(p,&old,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return old; }
static inline void atomic_or_uint(volatile unsigned *p,unsigned bits)
{ (void)__atomic_fetch_or(p,bits,__ATOMIC_SEQ_CST); }
static inline void atomic_and_uint(volatile unsigned *p,unsigned bits)
{ (void)__atomic_fetch_and(p,bits,__ATOMIC_SEQ_CST); }
static inline uint64_t atomic_inc_64_nv(volatile uint64_t *p)
{ return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST); }
#endif
