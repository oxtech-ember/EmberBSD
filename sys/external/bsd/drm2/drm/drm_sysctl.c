/*	$NetBSD: drm_sysctl.c,v 1.8 2021/12/19 11:36:57 riastradh Exp $	*/
/* Origin: EmberBSD; AI-assisted read-only native DRM PCI metadata. */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: drm_sysctl.c,v 1.8 2021/12/19 11:36:57 riastradh Exp $");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_native_identity.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
/* We need to specify the type programmatically */
#undef sysctl_createv

#include <drm/drm_sysctl.h>

#ifdef SYSCTL_INCLUDE_DESCR
static const char *
drm_sysctl_get_description(const struct linux_module_param_info *p,
    const struct drm_sysctl_def *def)
{
	const void * const *b = def->bd, * const *e = def->ed;

	for (; b < e; b++) {
		const struct linux_module_param_desc *d = *b;
		if (strcmp(p->dname, d->name) == 0)
			return d->description;
	}
	return NULL;
}
#endif

#ifdef notyet
static uint64_t
drm_sysctl_get_value(const struct linux_module_param_info *p)
{
	switch (p->type) {
	case MTYPE_bool:
		return *(bool *)p->ptr;
	case MTYPE_int:
		return *(int *)p->ptr;
	case MTYPE_uint:
		return *(unsigned *)p->ptr;
	default:
		aprint_error("unhandled module param type %d for %s\n",
		    p->type, p->name);
		return 0;
	}
}

static size_t
drm_sysctl_get_size(const struct linux_module_param_info *p)
{
	switch (p->type) {
	case MTYPE_bool:
		return sizeof(bool);
	case MTYPE_int:
		return sizeof(int);
	case MTYPE_uint:
		return sizeof(unsigned);
	default:
		aprint_error("unhandled module param type %d for %s\n",
		    p->type, p->name);
		return sizeof(void *);
	}
}
#endif

static int
drm_sysctl_get_type(const struct linux_module_param_info *p)
{
	switch (p->type) {
	case MTYPE_bool:
		return CTLTYPE_BOOL;
	case MTYPE_int:
		return CTLTYPE_INT;
	case MTYPE_charp:
		return CTLTYPE_STRING;
	case MTYPE_uint:
		return CTLTYPE_INT; /* XXX */
	default:
		aprint_error("unhandled module param type %d for %s\n",
		    p->type, p->name);
		return CTLTYPE_NODE;
	}
}


static int
drm_sysctl_node(const char *name, const struct sysctlnode **node,
    struct sysctllog **log)
{
	return sysctl_createv(log, 0, node, node,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, name, NULL,
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL);
}

void
drm_sysctl_init(struct drm_sysctl_def *def)
{
	const void * const *b = def->bp, * const *e = def->ep;
	const struct sysctlnode *rnode = NULL, *cnode;
	const char *name = "drm2";

	int error;
	if ((error = sysctl_createv(&def->log, 0, NULL, &rnode,
	    CTLFLAG_PERMANENT, CTLTYPE_NODE, name,
	    SYSCTL_DESCR("DRM driver parameters"),
	    NULL, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL)) != 0) {
		aprint_error("sysctl_createv returned %d, "
		    "for %s ignoring\n", error, name);
		return;
	}

	for (; b < e; b++) {
		const struct linux_module_param_info *p = *b;
		char copy[256], *n, *nn;
		strlcpy(copy, p->name, sizeof(copy));
		cnode = rnode;
		for (n = copy; (nn = strchr(n, '.')) != NULL; n = nn) {
			*nn++ = '\0';
			if ((error = drm_sysctl_node(n, &cnode, &def->log))
			    != 0) {
				aprint_error("sysctl_createv returned %d, "
				    "for %s ignoring\n", error, n);
				continue;
			}
		}
	        if ((error = sysctl_createv(&def->log, 0, &cnode,
		    &cnode, p->mode == 0600 ? CTLFLAG_READWRITE : 0,
		    drm_sysctl_get_type(p), n,
		    SYSCTL_DESCR(drm_sysctl_get_description(p, def)),
		    NULL, 0, p->ptr, 0, CTL_CREATE, CTL_EOL)) != 0)
			aprint_error("sysctl_createv returned %d, "
			    "for %s ignoring\n", error, n);
	}
}

void
drm_sysctl_fini(struct drm_sysctl_def *def)
{
	sysctl_teardown(&def->log);
}

/* sysctl lookup and teardown serialize immutable data with sysctl_treelock. */
struct drm_native_identity {
	struct sysctllog *log;
	struct drm_native_pci_record record;
	struct drm_native_platform_record platform;
};

int
drm_sysctl_identity_register(struct drm_device *dev)
{
	struct drm_native_identity *identity;
	struct drm_native_pci_record *r;
	struct drm_native_platform_record *p;
	void *record;
	size_t length;
	const char *leaf;
	const struct sysctlnode *root = NULL;
	const struct drm_minor *nodes[2] = { dev->primary, dev->render };
	devmajor_t maj;
	char name[32];
	unsigned int i;
	int error;

	/* Drivers without a cached record retain existing discovery. */
	if (dev->native_pci.version != DRM_NATIVE_PCI_VERSION &&
	    dev->native_platform.version != DRM_NATIVE_PLATFORM_VERSION)
		return 0;
	CTASSERT(sizeof(struct drm_native_pci_record) == 68);
	CTASSERT(sizeof(struct drm_native_platform_record) == 160);
	KASSERT(dev->native_identity == NULL);
	maj = cdevsw_lookup_major(&drm_cdevsw);
	if (maj == NODEVMAJOR || dev->primary == NULL)
		return -ENODEV;

	identity = kmem_zalloc(sizeof(*identity), KM_SLEEP);
	if (dev->native_pci.version == DRM_NATIVE_PCI_VERSION) {
		r = &identity->record;
		*r = dev->native_pci;
		r->flags |= DRM_NATIVE_PRIMARY;
		r->primary_major = maj;
		r->primary_minor = dev->primary->index;
		if (dev->render != NULL) {
			r->flags |= DRM_NATIVE_RENDER;
			r->render_major = maj;
			r->render_minor = dev->render->index;
		}
		record = r;
		length = sizeof(*r);
		leaf = "identity";
	} else {
		p = &identity->platform;
		*p = dev->native_platform;
		p->flags |= DRM_NATIVE_PRIMARY;
		p->primary_major = maj;
		p->primary_minor = dev->primary->index;
		if (dev->render != NULL) {
			p->flags |= DRM_NATIVE_RENDER;
			p->render_major = maj;
			p->render_minor = dev->render->index;
		}
		record = p;
		length = sizeof(*p);
		leaf = "platform";
	}

	/* The shared permanent parent belongs to the DRM core. */
	error = sysctl_createv(NULL, 0, NULL, &root, CTLFLAG_PERMANENT,
	    CTLTYPE_NODE, "drm2", NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (error)
		goto fail;
	for (i = 0; i < __arraycount(nodes); i++) {
		if (nodes[i] == NULL)
			continue;
		snprintf(name, sizeof(name), "%s%d", leaf, nodes[i]->index);
		error = sysctl_createv(&identity->log, 0, &root, NULL, 0,
		    CTLTYPE_STRUCT, name, SYSCTL_DESCR("DRM native identity"),
		    NULL, 0, record, length, CTL_CREATE, CTL_EOL);
		if (error)
			goto fail;
	}
	dev->native_identity = identity;
	return 0;

fail:
	/* A failed second leaf must not leave the first one published. */
	sysctl_teardown(&identity->log);
	kmem_free(identity, sizeof(*identity));
	return -error;
}

void
drm_sysctl_identity_unregister(struct drm_device *dev)
{
	struct drm_native_identity *identity = dev->native_identity;

	if (identity == NULL)
		return;
	/* This waits for in-flight sysctl lookups before freeing their data. */
	sysctl_teardown(&identity->log);
	dev->native_identity = NULL;
	kmem_free(identity, sizeof(*identity));
}
