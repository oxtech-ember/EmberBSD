/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
# 4 "d_c11_atomic_builtins.c"
/* lint1-extra-flags: -g -Ac11 -X 351 */

void
valid(_Atomic unsigned long *n, _Atomic(int *) *p, int **expected)
{
	_Static_assert(sizeof(__c11_atomic_load(n, 0)) == sizeof(unsigned long),
	    "atomic integer result width");
	_Static_assert(sizeof(__c11_atomic_load(p, 0)) == sizeof(int *),
	    "atomic pointer result width");
	_Static_assert(sizeof(__c11_atomic_is_lock_free(8)) == sizeof(_Bool),
	    "atomic boolean result width");
	unsigned long wide = __c11_atomic_load(n, 0);
	int *ptr = __c11_atomic_load(p, 0);
	int *swapped = __c11_atomic_exchange(p, ptr, 0);
	_Bool ok = __c11_atomic_compare_exchange_weak(p, expected, ptr, 0, 0);
	__c11_atomic_store(p, swapped, 0);
	__c11_atomic_thread_fence(0);
	(void)wide;
	(void)ok;
}

void
invalid(_Atomic int *p)
{
	(void)p;
	/* expect+1: error: cannot dereference non-pointer type 'int' [96] */
	(void)__c11_atomic_load(1, 0);
	/* expect+1: error: argument mismatch: 1 argument passed, 2 expected [150] */
	(void)__c11_atomic_load(0);
	/* expect+1: error: cannot initialize 'int' from 'void' [185] */
	int x = __c11_atomic_store(p, 1, 0);
	(void)x;
	/* expect+1: error: cannot dereference non-pointer type 'int' [96] */
	(void)__c11_atomic_compare_exchange_weak(p, 0, 1, 0, 0);
}

void
invalid_object(void *p)
{
	(void)p;
	/* expect+1: error: operand of '__c11_atomic_load' has invalid type 'pointer to void' [108] */
	(void)__c11_atomic_load(p, 0);
}
