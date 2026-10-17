/* SPDX-License-Identifier: BSD-2-Clause */
/* Static Citrus modules. Shared libc keeps the dynamic module loader. */

#include <sys/cdefs.h>
#include "namespace.h"
#include <sys/types.h>
#include <sys/queue.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_hash.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_iconv.h"
#include "citrus_mapper.h"

_CITRUS_STDENC_GETOPS_FUNC(UTF8);
_CITRUS_ICONV_GETOPS_FUNC(iconv_std);
_CITRUS_MAPPER_GETOPS_FUNC(mapper_646);
_CITRUS_MAPPER_GETOPS_FUNC(mapper_none);

enum citrus_static_interface {
	CITRUS_STATIC_STDENC,
	CITRUS_STATIC_ICONV,
	CITRUS_STATIC_MAPPER
};

struct _citrus_module_rec {
	const char *name;
	const char *interface;
	enum citrus_static_interface kind;
	union {
		_citrus_stdenc_getops_t stdenc;
		_citrus_iconv_getops_t iconv;
		_citrus_mapper_getops_t mapper;
	} getops;
};

/* Permanent handles. No caller can change or release this registry. */
static struct _citrus_module_rec modules[] = {
	{ "UTF8", "stdenc", CITRUS_STATIC_STDENC,
	    { .stdenc = _citrus_UTF8_stdenc_getops } },
	{ "iconv_std", "iconv", CITRUS_STATIC_ICONV,
	    { .iconv = _citrus_iconv_std_iconv_getops } },
	{ "mapper_646", "mapper", CITRUS_STATIC_MAPPER,
	    { .mapper = _citrus_mapper_646_mapper_getops } },
	{ "mapper_none", "mapper", CITRUS_STATIC_MAPPER,
	    { .mapper = _citrus_mapper_none_mapper_getops } }
};

int
_citrus_load_module(_citrus_module_t *rhandle, const char *modname)
{
	size_t i;

	if (rhandle == NULL || modname == NULL)
		return EINVAL;
	for (i = 0; i < __arraycount(modules); i++) {
		if (strcmp(modname, modules[i].name) == 0) {
			*rhandle = &modules[i];
			return 0;
		}
	}
	return EINVAL;
}

void *
_citrus_find_getops(_citrus_module_t handle, const char *modname,
    const char *ifname)
{
	size_t i;

	if (modname == NULL || ifname == NULL)
		return NULL;
	for (i = 0; i < __arraycount(modules); i++) {
		if (handle != &modules[i])
			continue;
		if (strcmp(modname, modules[i].name) != 0 ||
		    strcmp(ifname, modules[i].interface) != 0)
			return NULL;
		switch (modules[i].kind) {
		case CITRUS_STATIC_STDENC:
			return (void *)modules[i].getops.stdenc;
		case CITRUS_STATIC_ICONV:
			return (void *)modules[i].getops.iconv;
		case CITRUS_STATIC_MAPPER:
			return (void *)modules[i].getops.mapper;
		}
	}
	return NULL;
}

void
_citrus_unload_module(_citrus_module_t handle)
{

	/* Static handles and their operation tables have process lifetime. */
	(void)handle;
}
