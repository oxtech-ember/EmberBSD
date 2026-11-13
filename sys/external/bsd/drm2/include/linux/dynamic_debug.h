/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/dynamic_debug.h
 * (GPL-2.0; no source copied).
 *
 * Types only.  There is no dynamic debug control on this layer, so
 * DECLARE_DYNDBG_CLASSMAP expands to nothing and DRM keeps its debug
 * categories in drm_debug (CONFIG_DRM_USE_DYNAMIC_DEBUG is not defined).
 */

#ifndef _LINUX_DYNAMIC_DEBUG_H_
#define _LINUX_DYNAMIC_DEBUG_H_

#include <linux/types.h>

struct _ddebug {
	const char *modname;
	const char *function;
	const char *filename;
	const char *format;
	unsigned int lineno:18;
	unsigned int class_id:6;
	unsigned int flags:8;
};

enum ddebug_class_map_type {
	DD_CLASS_TYPE_DISJOINT_BITS,
	DD_CLASS_TYPE_LEVEL_NUM,
	DD_CLASS_TYPE_DISJOINT_NAMES,
	DD_CLASS_TYPE_LEVEL_NAMES,
};

struct ddebug_class_map {
	const char *mod_name;
	const char **class_names;
	int length;
	int base;
	enum ddebug_class_map_type map_type;
};

struct ddebug_class_param {
	union {
		unsigned long *bits;
		unsigned int *lvl;
	};
	char flags[8];
	const struct ddebug_class_map *map;
};

#define	DECLARE_DYNDBG_CLASSMAP(_var, _maptype, _base, ...)
#define	DYNDBG_CLASSMAP_PARAM(_name, _var, _flags)
#define	DYNDBG_CLASSMAP_PARAM_REF(_name, _bits, _flags, _modnm, _depmap)

#endif	/* _LINUX_DYNAMIC_DEBUG_H_ */
