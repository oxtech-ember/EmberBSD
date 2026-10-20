/* SPDX-License-Identifier: BSD-2-Clause */
/* Compile the actual native fixed-workspace sort with controlled assertions. */
#ifndef BCMV3D_KHEAPSORT_MODEL_H
#define BCMV3D_KHEAPSORT_MODEL_H
#ifndef _DIAGASSERT
#define _DIAGASSERT(e) CHECK(e)
#endif
#ifndef _KERNEL
#define _KERNEL
#define BCMV3D_SORT_UNDEF_KERNEL
#endif
#include "heapsort-body.h"
#ifdef BCMV3D_SORT_UNDEF_KERNEL
#undef _KERNEL
#undef BCMV3D_SORT_UNDEF_KERNEL
#endif
#endif
