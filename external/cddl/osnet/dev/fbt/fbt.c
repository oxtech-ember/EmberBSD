/*	$NetBSD: fbt.c,v 1.29 2022/09/02 11:03:50 riastradh Exp $	*/

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2006-2008 John Birrell jb@freebsd.org
 * Portions Copyright 2010 Darran Hunt darran@NetBSD.org
 *
 * $FreeBSD: head/sys/cddl/dev/fbt/fbt.c 309786 2016-12-10 03:13:11Z markj $
 *
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cdefs.h>
#include <sys/proc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cpuvar.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/ksyms.h>
#include <sys/cpu.h>
#include <sys/kthread.h>
#include <sys/syslimits.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/exec_elf.h>

#include <sys/dtrace.h>
#include <sys/dtrace_bsd.h>
#include <sys/kern_ctf.h>
#include <sys/dtrace_impl.h>

#include "fbt.h"
#include "fbt_ctf.h"

mod_ctf_t *modptr;

dtrace_provider_id_t	fbt_id;
fbt_probe_t		**fbt_probetab;
int			fbt_probetab_mask;
static int			fbt_probetab_size;

static dev_type_open(fbt_open);
static int	fbt_unload(void);
static void	fbt_getargdesc(void *, dtrace_id_t, void *, dtrace_argdesc_t *);
static void	fbt_provide_module(void *, modctl_t *);
static void	fbt_destroy(void *, dtrace_id_t, void *);
static int	fbt_enable(void *, dtrace_id_t, void *);
static void	fbt_disable(void *, dtrace_id_t, void *);
static void	fbt_load(void);
static void	fbt_suspend(void *, dtrace_id_t, void *);
static void	fbt_resume(void *, dtrace_id_t, void *);

static const struct cdevsw fbt_cdevsw = {
	.d_open		= fbt_open,
	.d_close	= noclose,
	.d_read		= noread,
	.d_write	= nowrite,
	.d_ioctl	= noioctl,
	.d_stop		= nostop,
	.d_tty		= notty,
	.d_poll		= nopoll,
	.d_mmap		= nommap,
	.d_kqfilter	= nokqfilter,
	.d_discard	= nodiscard,
	.d_flag		= D_OTHER
};

static dtrace_pattr_t fbt_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pops_t fbt_pops = {
	NULL,
	fbt_provide_module,
	fbt_enable,
	fbt_disable,
	fbt_suspend,
	fbt_resume,
	fbt_getargdesc,
	NULL,
	NULL,
	fbt_destroy
};

#ifdef __FreeBSD__
static int			fbt_verbose = 0;

static struct cdev		*fbt_cdev;
#endif /* __FreeBSD__ */

#ifdef __NetBSD__
specificdata_key_t fbt_module_key;

#define version xversion
#endif /* __NetBSD__ */

int
fbt_excluded(const char *name)
{

	if (strncmp(name, "dtrace_", 7) == 0 &&
	    strncmp(name, "dtrace_safe_", 12) != 0) {
		/*
		 * Anything beginning with "dtrace_" may be called
		 * from probe context unless it explicitly indicates
		 * that it won't be called from probe context by
		 * using the prefix "dtrace_safe_".
		 */
		return (1);
	}

#ifdef __FreeBSD__
	/*
	 * Lock owner methods may be called from probe context.
	 */
	if (strcmp(name, "owner_mtx") == 0 ||
	    strcmp(name, "owner_rm") == 0 ||
	    strcmp(name, "owner_rw") == 0 ||
	    strcmp(name, "owner_sx") == 0)
		return (1);

	/*
	 * When DTrace is built into the kernel we need to exclude
	 * the FBT functions from instrumentation.
	 */
#ifndef _KLD_MODULE
	if (strncmp(name, "fbt_", 4) == 0)
		return (1);
#endif
#endif

#ifdef __NetBSD__
	if (strcmp(name, "cpu_index") == 0 ||
	    strncmp(name, "db_", 3) == 0 ||
	    strncmp(name, "ddb_", 4) == 0 ||
	    strncmp(name, "kdb_", 4) == 0 ||
	    strncmp(name, "lockdebug_", 10) == 0 ||
	    strncmp(name, "kauth_", 5) == 0 ||
	    strncmp(name, "ktext_write", 11) == 0 ||
	    strncmp(name, "fbt_", 4) == 0) {
		return (1);
	}
#endif

	return (0);
}

static void
fbt_doubletrap(void)
{
	fbt_probe_t *fbt;
	int i;

	for (i = 0; i < fbt_probetab_size; i++) {
		fbt = fbt_probetab[i];

		for (; fbt != NULL; fbt = fbt->fbtp_next)
			fbt_patch_tracepoint(fbt, fbt->fbtp_savedval);
	}
}

#ifdef __FreeBSD__
static void
fbt_provide_module(void *arg, modctl_t *lf)
{
	char modname[MAXPATHLEN];
	int i;
	size_t len;

	strlcpy(modname, lf->filename, sizeof(modname));
	len = strlen(modname);
	if (len > 3 && strcmp(modname + len - 3, ".ko") == 0)
		modname[len - 3] = '\0';

	/*
	 * Employees of dtrace and their families are ineligible.  Void
	 * where prohibited.
	 */
	if (strcmp(modname, "dtrace") == 0)
		return;

	/*
	 * To register with DTrace, a module must list 'dtrace' as a
	 * dependency in order for the kernel linker to resolve
	 * symbols like dtrace_register(). All modules with such a
	 * dependency are ineligible for FBT tracing.
	 */
	for (i = 0; i < lf->ndeps; i++)
		if (strncmp(lf->deps[i]->filename, "dtrace", 6) == 0)
			return;

	if (lf->fbt_nentries) {
		/*
		 * This module has some FBT entries allocated; we're afraid
		 * to screw with it.
		 */
		return;
	}

	/*
	 * List the functions in the module and the symbol values.
	 */
	(void) linker_file_function_listall(lf, fbt_provide_module_function, modname);
}
#endif
#ifdef __NetBSD__
static void
fbt_provide_module(void *arg, modctl_t *mod)
{
	struct fbt_ksyms_arg fka;
	struct mod_ctf *mc;
	char modname[MAXPATHLEN];
	int i;
	size_t len;

	if (mod_ctf_get(mod, &mc)) {
		printf("fbt: no CTF data for module %s\n", module_name(mod));
		return;
	}

	strlcpy(modname, module_name(mod), sizeof(modname));
	len = strlen(modname);
	if (len > 5 && strcmp(modname + len - 3, ".kmod") == 0)
		modname[len - 4] = '\0';

	/*
	 * Employees of dtrace and their families are ineligible.  Void
	 * where prohibited.
	 */
	if (strcmp(modname, "dtrace") == 0)
		return;

	/*
	 * The cyclic timer subsystem can be built as a module and DTrace
	 * depends on that, so it is ineligible too.
	 */
	if (strcmp(modname, "cyclic") == 0)
		return;

	/*
	 * To register with DTrace, a module must list 'dtrace' as a
	 * dependency in order for the kernel linker to resolve
	 * symbols like dtrace_register(). All modules with such a
	 * dependency are ineligible for FBT tracing.
	 */
	for (i = 0; i < mod->mod_nrequired; i++) {
		if (strncmp(module_name((*mod->mod_required)[i]),
			    "dtrace", 6) == 0)
			return;
	}
	if (mc->fbt_provided) {
		return;
	}

	/*
	 * List the functions in the module and the symbol values.
	 */
	memset(&fka, 0, sizeof(fka));
	fka.fka_mod = mod;
	fka.fka_mc = mc;
	ksyms_mod_foreach(modname, fbt_provide_module_cb, &fka);
	mc->fbt_provided = true;
}

static void
fbt_module_dtor(void *arg)
{
	mod_ctf_t *mc = arg;

	if (mc->ctfalloc)
		free(mc->ctftab, M_TEMP);
	kmem_free(mc, sizeof(*mc));
}
#endif

static void
fbt_destroy(void *arg, dtrace_id_t id, void *parg)
{
	fbt_probe_t *fbt = parg, *next, *hash, *last;
	modctl_t *ctl;
	int ndx;

	do {
		ctl = fbt->fbtp_ctl;

#ifdef __FreeBSD__
		ctl->mod_fbtentries--;
#endif
#ifdef __NetBSD__
		mod_ctf_t *mc = module_getspecific(ctl, fbt_module_key);
		mc->fbt_provided = false;
#endif

		/*
		 * Now we need to remove this probe from the fbt_probetab.
		 */
		ndx = FBT_ADDR2NDX(fbt->fbtp_patchpoint);
		last = NULL;
		hash = fbt_probetab[ndx];

		while (hash != fbt) {
			ASSERT(hash != NULL);
			last = hash;
			hash = hash->fbtp_hashnext;
		}

		if (last != NULL) {
			last->fbtp_hashnext = fbt->fbtp_hashnext;
		} else {
			fbt_probetab[ndx] = fbt->fbtp_hashnext;
		}

		next = fbt->fbtp_next;
		kmem_free(fbt, sizeof(*fbt));

		fbt = next;
	} while (fbt != NULL);
}

static int
fbt_enable(void *arg, dtrace_id_t id, void *parg)
{
	fbt_probe_t *fbt = parg;
	modctl_t *ctl = fbt->fbtp_ctl;

#ifdef __NetBSD__
	module_hold(ctl);
#else
	ctl->nenabled++;

	/*
	 * Now check that our modctl has the expected load count.  If it
	 * doesn't, this module must have been unloaded and reloaded -- and
	 * we're not going to touch it.
	 */
	if (ctl->loadcnt != fbt->fbtp_loadcnt) {
		if (fbt_verbose) {
			printf("fbt is failing for probe %s "
			    "(module %s reloaded)",
			    fbt->fbtp_name, module_name(ctl));
		}

		return 0;
	}
#endif

	for (; fbt != NULL; fbt = fbt->fbtp_next)
		fbt_patch_tracepoint(fbt, fbt->fbtp_patchval);
	return 0;
}

static void
fbt_disable(void *arg, dtrace_id_t id, void *parg)
{
	fbt_probe_t *fbt = parg;
	modctl_t *ctl = fbt->fbtp_ctl;

#ifndef __NetBSD__
	ASSERT(ctl->nenabled > 0);
	ctl->nenabled--;

	if ((ctl->loadcnt != fbt->fbtp_loadcnt))
		return;
#endif

	for (; fbt != NULL; fbt = fbt->fbtp_next)
		fbt_patch_tracepoint(fbt, fbt->fbtp_savedval);

#ifdef __NetBSD__
	module_rele(ctl);
#endif
}

static void
fbt_suspend(void *arg, dtrace_id_t id, void *parg)
{
	fbt_probe_t *fbt = parg;
#ifndef __NetBSD__
	modctl_t *ctl = fbt->fbtp_ctl;

	ASSERT(ctl->nenabled > 0);

	if ((ctl->loadcnt != fbt->fbtp_loadcnt))
		return;
#endif

	for (; fbt != NULL; fbt = fbt->fbtp_next)
		fbt_patch_tracepoint(fbt, fbt->fbtp_savedval);
}

static void
fbt_resume(void *arg, dtrace_id_t id, void *parg)
{
	fbt_probe_t *fbt = parg;
#ifndef __NetBSD__
	modctl_t *ctl = fbt->fbtp_ctl;

	ASSERT(ctl->nenabled > 0);

	if ((ctl->loadcnt != fbt->fbtp_loadcnt))
		return;
#endif

	for (; fbt != NULL; fbt = fbt->fbtp_next)
		fbt_patch_tracepoint(fbt, fbt->fbtp_patchval);
}

static int
fbt_ctfoff_init(modctl_t *mod, mod_ctf_t *mc)
{
	const Elf_Sym *symp = mc->symtab;
	const ctf_header_t *hp = (const ctf_header_t *) mc->ctftab;
	const uint8_t *ctfdata = mc->ctftab + sizeof(ctf_header_t);
	int i;
	uint32_t *ctfoff;
	uint32_t objtoff = hp->cth_objtoff;
	uint32_t funcoff = hp->cth_funcoff;
	uint32_t info, vlen;
	size_t width = fbt_ctf_width(hp->cth_version);
	int nsyms = (mc->nmap != NULL) ? mc->nmapsize : mc->nsym;

	/* Sanity check. */
	if (mc->ctfcnt < sizeof(*hp) || hp->cth_magic != CTF_MAGIC ||
	    (hp->cth_version != CTF_VERSION_2 &&
	    hp->cth_version != CTF_VERSION_3) ||
	    hp->cth_objtoff > hp->cth_funcoff ||
	    hp->cth_funcoff > hp->cth_typeoff ||
	    hp->cth_typeoff > hp->cth_stroff ||
	    hp->cth_stroff > mc->ctfcnt - sizeof(*hp) ||
	    hp->cth_strlen > mc->ctfcnt - sizeof(*hp) - hp->cth_stroff) {
		printf("Bad magic value in CTF data of '%s'\n",
		    module_name(mod));
		return (EINVAL);
	}

	if (mc->symtab == NULL) {
		printf("No symbol table in '%s'\n", module_name(mod));
		return (EINVAL);
	}

	ctfoff = malloc(sizeof(uint32_t) * nsyms, M_FBT, M_WAITOK);
	memset(ctfoff, 0xff, sizeof(uint32_t) * nsyms);
	mc->ctfoffp = ctfoff;

	for (i = 0; i < nsyms; i++, ctfoff++, symp++) {
	   	if (mc->nmap != NULL) {
			if (mc->nmap[i] == 0 || mc->nmap[i] > mc->nsym) {
				printf("%s.%d: Error! Got zero nmap!\n",
					__func__, __LINE__);
				free(mc->ctfoffp, M_FBT);
				mc->ctfoffp = NULL;
				return EINVAL;
			}

			/*
			 * CTF expects the unsorted symbol ordering,
			 * so map it from that to the current sorted
			 * symbol table.
			 * ctfoff[new-ind] = oldind symbol info.
			 */

			/* map old index to new symbol table */
			symp = &mc->symtab[mc->nmap[i] - 1];

			/* map old index to new ctfoff index */
			ctfoff = &mc->ctfoffp[mc->nmap[i]-1];
		}

		/*
		 * Note that due to how kern_ksyms.c adjusts st_name
		 * to be the offset into a virtual combined strtab,
		 * st_name will never be 0 for loaded modules.
		 */

		if (symp->st_name == 0 || symp->st_shndx == SHN_UNDEF) {
			*ctfoff = 0xffffffff;
			continue;
		}

		switch (ELF_ST_TYPE(symp->st_info)) {
		case STT_OBJECT:
			if (objtoff > hp->cth_funcoff ||
			    width > hp->cth_funcoff - objtoff ||
                            (symp->st_shndx == SHN_ABS && symp->st_value == 0)) {
				*ctfoff = 0xffffffff;
                                break;
                        }

                        *ctfoff = objtoff;
                        objtoff += width;
			break;

		case STT_FUNC:
			if (funcoff > hp->cth_typeoff ||
			    width > hp->cth_typeoff - funcoff) {
				*ctfoff = 0xffffffff;
				break;
			}

			*ctfoff = funcoff;

			info = fbt_ctf_word(ctfdata + funcoff, width);
			vlen = fbt_ctf_vlen(hp->cth_version, info);

			/*
			 * If we encounter a zero pad at the end, just skip it.
			 * Otherwise skip over the function and its return type
			 * (+2) and the argument list (vlen).
			 */
			if (fbt_ctf_kind(hp->cth_version, info) == CTF_K_UNKNOWN && vlen == 0)
				funcoff += width; /* skip pad */
			else if (fbt_ctf_kind(hp->cth_version, info) != CTF_K_FUNCTION ||
			    vlen + 2 > (hp->cth_typeoff - funcoff) / width) {
				free(mc->ctfoffp, M_FBT);
				mc->ctfoffp = NULL;
				return EINVAL;
			} else
				funcoff += width * (vlen + 2);
			break;

		default:
			*ctfoff = 0xffffffff;
			break;
		}
	}

	return (0);
}

/* Origin: EmberBSD (AI-assisted), index CTF2 and CTF3 without ABI casts. */
static int
fbt_typoff_init(mod_ctf_t *mc)
{
	const ctf_header_t *hp = (const ctf_header_t *)mc->ctftab;
	const uint8_t *data = mc->ctftab + sizeof(*hp);
	struct fbt_ctf_type t;
	size_t off, count = 1, i;

	for (off = hp->cth_typeoff; off < hp->cth_stroff; off += t.reclen) {
		if (fbt_ctf_decode(hp->cth_version, data + off,
		    hp->cth_stroff - off, &t) != 0)
			return EINVAL;
		count++;
	}
	mc->typoffp = malloc(sizeof(uint32_t) * count, M_FBT, M_ZERO | M_WAITOK);
	mc->typlen = count;
	for (off = hp->cth_typeoff, i = 1; i < count; i++) {
		(void)fbt_ctf_decode(hp->cth_version, data + off,
		    hp->cth_stroff - off, &t);
		mc->typoffp[i] = off;
		off += t.reclen;
	}
	return 0;
}

/*
 * CTF Declaration Stack
 *
 * In order to implement ctf_type_name(), we must convert a type graph back
 * into a C type declaration.  Unfortunately, a type graph represents a storage
 * class ordering of the type whereas a type declaration must obey the C rules
 * for operator precedence, and the two orderings are frequently in conflict.
 * For example, consider these CTF type graphs and their C declarations:
 *
 * CTF_K_POINTER -> CTF_K_FUNCTION -> CTF_K_INTEGER  : int (*)()
 * CTF_K_POINTER -> CTF_K_ARRAY -> CTF_K_INTEGER     : int (*)[]
 *
 * In each case, parentheses are used to raise operator * to higher lexical
 * precedence, so the string form of the C declaration cannot be constructed by
 * walking the type graph links and forming the string from left to right.
 *
 * The functions in this file build a set of stacks from the type graph nodes
 * corresponding to the C operator precedence levels in the appropriate order.
 * The code in ctf_type_name() can then iterate over the levels and nodes in
 * lexical precedence order and construct the final C declaration string.
 */
typedef struct ctf_list {
	struct ctf_list *l_prev; /* previous pointer or tail pointer */
	struct ctf_list *l_next; /* next pointer or head pointer */
} ctf_list_t;

#define	ctf_list_prev(elem)	((void *)(((ctf_list_t *)(elem))->l_prev))
#define	ctf_list_next(elem)	((void *)(((ctf_list_t *)(elem))->l_next))

typedef enum {
	CTF_PREC_BASE,
	CTF_PREC_POINTER,
	CTF_PREC_ARRAY,
	CTF_PREC_FUNCTION,
	CTF_PREC_MAX
} ctf_decl_prec_t;

typedef struct ctf_decl_node {
	ctf_list_t cd_list;			/* linked list pointers */
	ctf_id_t cd_type;			/* type identifier */
	uint_t cd_kind;				/* type kind */
	uint_t cd_n;				/* type dimension if array */
} ctf_decl_node_t;

typedef struct ctf_decl {
	ctf_list_t cd_nodes[CTF_PREC_MAX];	/* declaration node stacks */
	int cd_order[CTF_PREC_MAX];		/* storage order of decls */
	ctf_decl_prec_t cd_qualp;		/* qualifier precision */
	ctf_decl_prec_t cd_ordp;		/* ordered precision */
	char *cd_buf;				/* buffer for output */
	char *cd_ptr;				/* buffer location */
	char *cd_end;				/* buffer limit */
	size_t cd_len;				/* buffer space required */
	int cd_err;				/* saved error value */
} ctf_decl_t;

/*
 * Simple doubly-linked list append routine.  This implementation assumes that
 * each list element contains an embedded ctf_list_t as the first member.
 * An additional ctf_list_t is used to store the head (l_next) and tail
 * (l_prev) pointers.  The current head and tail list elements have their
 * previous and next pointers set to NULL, respectively.
 */
static void
ctf_list_append(ctf_list_t *lp, void *new)
{
	ctf_list_t *p = lp->l_prev;	/* p = tail list element */
	ctf_list_t *q = new;		/* q = new list element */

	lp->l_prev = q;
	q->l_prev = p;
	q->l_next = NULL;

	if (p != NULL)
		p->l_next = q;
	else
		lp->l_next = q;
}

/*
 * Prepend the specified existing element to the given ctf_list_t.  The
 * existing pointer should be pointing at a struct with embedded ctf_list_t.
 */
static void
ctf_list_prepend(ctf_list_t *lp, void *new)
{
	ctf_list_t *p = new;		/* p = new list element */
	ctf_list_t *q = lp->l_next;	/* q = head list element */

	lp->l_next = p;
	p->l_prev = NULL;
	p->l_next = q;

	if (q != NULL)
		q->l_prev = p;
	else
		lp->l_prev = p;
}

static void
ctf_decl_init(ctf_decl_t *cd, char *buf, size_t len)
{
	int i;

	bzero(cd, sizeof (ctf_decl_t));

	for (i = CTF_PREC_BASE; i < CTF_PREC_MAX; i++)
		cd->cd_order[i] = CTF_PREC_BASE - 1;

	cd->cd_qualp = CTF_PREC_BASE;
	cd->cd_ordp = CTF_PREC_BASE;

	cd->cd_buf = buf;
	cd->cd_ptr = buf;
	cd->cd_end = buf + len;
}

static void
ctf_decl_fini(ctf_decl_t *cd)
{
	ctf_decl_node_t *cdp, *ndp;
	int i;

	for (i = CTF_PREC_BASE; i < CTF_PREC_MAX; i++) {
		for (cdp = ctf_list_next(&cd->cd_nodes[i]);
		    cdp != NULL; cdp = ndp) {
			ndp = ctf_list_next(cdp);
			free(cdp, M_FBT);
		}
	}
}

static const uint8_t *
ctf_lookup_by_id(mod_ctf_t *mc, ctf_id_t type, struct fbt_ctf_type *t)
{
	const ctf_header_t *hp = (const ctf_header_t *)mc->ctftab;
	const uint8_t *p;
	uint32_t off;

	if (type <= 0 || type >= mc->typlen)
		return NULL;
	off = mc->typoffp[type];
	if (off < hp->cth_typeoff || off >= hp->cth_stroff)
		return NULL;
	p = mc->ctftab + sizeof(*hp) + off;
	if (fbt_ctf_decode(hp->cth_version, p, hp->cth_stroff - off, t) != 0)
		return NULL;
	return p;
}

static void
fbt_array_info(mod_ctf_t *mc, ctf_id_t type, ctf_arinfo_t *arp)
{
	const ctf_header_t *hp = (const ctf_header_t *)mc->ctftab;
	const uint8_t *p;
	struct fbt_ctf_type t;
	size_t width = fbt_ctf_width(hp->cth_version);

	bzero(arp, sizeof(*arp));
	p = ctf_lookup_by_id(mc, type, &t);
	if (p == NULL || t.kind != CTF_K_ARRAY)
		return;
	p += t.hdrlen;
	arp->ctr_contents = fbt_ctf_word(p, width);
	arp->ctr_index = fbt_ctf_word(p + width, width);
	arp->ctr_nelems = fbt_ctf_word(p + 2 * width, 4);
}

static const char *
ctf_strptr(mod_ctf_t *mc, int name)
{
	const ctf_header_t *hp = (const ctf_header_t *) mc->ctftab;
	const char *strp = "";

	if (name < 0 || name >= hp->cth_strlen)
		return(strp);

	strp = (const char *)(mc->ctftab + hp->cth_stroff + name + sizeof(ctf_header_t));

	if (memchr(strp, 0, hp->cth_strlen - name) == NULL)
		return "";
	return (strp);
}

static void
ctf_decl_push(ctf_decl_t *cd, mod_ctf_t *mc, ctf_id_t type)
{
	ctf_decl_node_t *cdp;
	ctf_decl_prec_t prec;
	uint_t kind, n = 1;
	int is_qual = 0;

	struct fbt_ctf_type t;
	ctf_arinfo_t ar;

	if (ctf_lookup_by_id(mc, type, &t) == NULL) {
		cd->cd_err = ENOENT;
		return;
	}

	switch (kind = t.kind) {
	case CTF_K_ARRAY:
		fbt_array_info(mc, type, &ar);
		ctf_decl_push(cd, mc, ar.ctr_contents);
		n = ar.ctr_nelems;
		prec = CTF_PREC_ARRAY;
		break;

	case CTF_K_TYPEDEF:
		if (ctf_strptr(mc, t.name)[0] == '\0') {
			ctf_decl_push(cd, mc, t.ref);
			return;
		}
		prec = CTF_PREC_BASE;
		break;

	case CTF_K_FUNCTION:
		ctf_decl_push(cd, mc, t.ref);
		prec = CTF_PREC_FUNCTION;
		break;

	case CTF_K_POINTER:
		ctf_decl_push(cd, mc, t.ref);
		prec = CTF_PREC_POINTER;
		break;

	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		ctf_decl_push(cd, mc, t.ref);
		prec = cd->cd_qualp;
		is_qual++;
		break;

	default:
		prec = CTF_PREC_BASE;
	}

	cdp = malloc(sizeof(*cdp), M_FBT, M_WAITOK);
	cdp->cd_type = type;
	cdp->cd_kind = kind;
	cdp->cd_n = n;

	if (ctf_list_next(&cd->cd_nodes[prec]) == NULL)
		cd->cd_order[prec] = cd->cd_ordp++;

	/*
	 * Reset cd_qualp to the highest precedence level that we've seen so
	 * far that can be qualified (CTF_PREC_BASE or CTF_PREC_POINTER).
	 */
	if (prec > cd->cd_qualp && prec < CTF_PREC_ARRAY)
		cd->cd_qualp = prec;

	/*
	 * C array declarators are ordered inside out so prepend them.  Also by
	 * convention qualifiers of base types precede the type specifier (e.g.
	 * const int vs. int const) even though the two forms are equivalent.
	 */
	if (kind == CTF_K_ARRAY || (is_qual && prec == CTF_PREC_BASE))
		ctf_list_prepend(&cd->cd_nodes[prec], cdp);
	else
		ctf_list_append(&cd->cd_nodes[prec], cdp);
}

static void
ctf_decl_sprintf(ctf_decl_t *cd, const char *format, ...)
{
	size_t len = (size_t)(cd->cd_end - cd->cd_ptr);
	va_list ap;
	size_t n;

	va_start(ap, format);
	n = vsnprintf(cd->cd_ptr, len, format, ap);
	va_end(ap);

	cd->cd_ptr += MIN(n, len);
	cd->cd_len += n;
}

static ssize_t
fbt_type_name(mod_ctf_t *mc, ctf_id_t type, char *buf, size_t len)
{
	ctf_decl_t cd;
	ctf_decl_node_t *cdp;
	ctf_decl_prec_t prec, lp, rp;
	int ptr, arr;
	uint_t k;

	if (mc == NULL && type == CTF_ERR)
		return (-1); /* simplify caller code by permitting CTF_ERR */

	ctf_decl_init(&cd, buf, len);
	ctf_decl_push(&cd, mc, type);

	if (cd.cd_err != 0) {
		ctf_decl_fini(&cd);
		return (-1);
	}

	/*
	 * If the type graph's order conflicts with lexical precedence order
	 * for pointers or arrays, then we need to surround the declarations at
	 * the corresponding lexical precedence with parentheses.  This can
	 * result in either a parenthesized pointer (*) as in int (*)() or
	 * int (*)[], or in a parenthesized pointer and array as in int (*[])().
	 */
	ptr = cd.cd_order[CTF_PREC_POINTER] > CTF_PREC_POINTER;
	arr = cd.cd_order[CTF_PREC_ARRAY] > CTF_PREC_ARRAY;

	rp = arr ? CTF_PREC_ARRAY : ptr ? CTF_PREC_POINTER : -1;
	lp = ptr ? CTF_PREC_POINTER : arr ? CTF_PREC_ARRAY : -1;

	k = CTF_K_POINTER; /* avoid leading whitespace (see below) */

	for (prec = CTF_PREC_BASE; prec < CTF_PREC_MAX; prec++) {
		for (cdp = ctf_list_next(&cd.cd_nodes[prec]);
		    cdp != NULL; cdp = ctf_list_next(cdp)) {

			struct fbt_ctf_type t;
			const char *name;

			if (ctf_lookup_by_id(mc, cdp->cd_type, &t) == NULL) {
				ctf_decl_fini(&cd);
				return -1;
			}
			name = ctf_strptr(mc, t.name);

			if (k != CTF_K_POINTER && k != CTF_K_ARRAY)
				ctf_decl_sprintf(&cd, " ");

			if (lp == prec) {
				ctf_decl_sprintf(&cd, "(");
				lp = -1;
			}

			switch (cdp->cd_kind) {
			case CTF_K_INTEGER:
			case CTF_K_FLOAT:
			case CTF_K_TYPEDEF:
				ctf_decl_sprintf(&cd, "%s", name);
				break;
			case CTF_K_POINTER:
				ctf_decl_sprintf(&cd, "*");
				break;
			case CTF_K_ARRAY:
				ctf_decl_sprintf(&cd, "[%u]", cdp->cd_n);
				break;
			case CTF_K_FUNCTION:
				ctf_decl_sprintf(&cd, "()");
				break;
			case CTF_K_STRUCT:
			case CTF_K_FORWARD:
				ctf_decl_sprintf(&cd, "struct %s", name);
				break;
			case CTF_K_UNION:
				ctf_decl_sprintf(&cd, "union %s", name);
				break;
			case CTF_K_ENUM:
				ctf_decl_sprintf(&cd, "enum %s", name);
				break;
			case CTF_K_VOLATILE:
				ctf_decl_sprintf(&cd, "volatile");
				break;
			case CTF_K_CONST:
				ctf_decl_sprintf(&cd, "const");
				break;
			case CTF_K_RESTRICT:
				ctf_decl_sprintf(&cd, "restrict");
				break;
			}

			k = cdp->cd_kind;
		}

		if (rp == prec)
			ctf_decl_sprintf(&cd, ")");
	}

	ctf_decl_fini(&cd);
	return (cd.cd_len);
}

static void
fbt_getargdesc(void *arg __unused, dtrace_id_t id __unused, void *parg, dtrace_argdesc_t *desc)
{
	const uint8_t *dp;
	const ctf_header_t *hp;
	size_t width;
	fbt_probe_t *fbt = parg;
	mod_ctf_t *mc;
	modctl_t *ctl = fbt->fbtp_ctl;
	int ndx = desc->dtargd_ndx;
	int symindx = fbt->fbtp_symindx;
	uint32_t *ctfoff;
	uint32_t offset;
	uint32_t info, kind, n;
	int nsyms;

	if (fbt->fbtp_roffset != 0 && desc->dtargd_ndx == 0) {
		(void) strcpy(desc->dtargd_native, "int");
		return;
	}

	desc->dtargd_ndx = DTRACE_ARGNONE;

	/* Get a pointer to the CTF data and its length. */
	if (mod_ctf_get(ctl, &mc) != 0) {
		static int report = 0;
		if (report < 1) {
			report++;
			printf("FBT: Error no CTF section found in module \"%s\"\n",
			    module_name(ctl));
		}
		/* No CTF data? Something wrong? *shrug* */
		return;
	}

	nsyms = (mc->nmap != NULL) ? mc->nmapsize : mc->nsym;

	/* Check if this module hasn't been initialised yet. */
	if (mc->ctfoffp == NULL) {
		/*
		 * Initialise the CTF object and function symindx to
		 * byte offset array.
		 */
		if (fbt_ctfoff_init(ctl, mc) != 0)
			return;

	}
	/* A failed type-table initialization must remain retryable. */
	if (mc->typoffp == NULL && fbt_typoff_init(mc) != 0)
		return;

	ctfoff = mc->ctfoffp;

	if (ctfoff == NULL || mc->typoffp == NULL) {
		return;
	}

	/* Check if the symbol index is out of range. */
	if (symindx >= nsyms)
		return;

	/* Check if the symbol isn't cross-referenced. */
	if ((offset = ctfoff[symindx]) == 0xffffffff)
		return;

	hp = (const ctf_header_t *)mc->ctftab;
	width = fbt_ctf_width(hp->cth_version);
	dp = mc->ctftab + offset + sizeof(*hp);
	info = fbt_ctf_word(dp, width);
	dp += width;
	kind = fbt_ctf_kind(hp->cth_version, info);
	n = fbt_ctf_vlen(hp->cth_version, info);

	if (kind == CTF_K_UNKNOWN && n == 0) {
		printf("%s(%d): Unknown function %s!\n",__func__,__LINE__,
		    fbt->fbtp_name);
		return;
	}

	if (kind != CTF_K_FUNCTION) {
		printf("%s(%d): Expected a function %s!\n",__func__,__LINE__,
		    fbt->fbtp_name);
		return;
	}

	if (fbt->fbtp_roffset != 0) {
		/* Only return type is available for args[1] in return probe. */
		if (ndx > 1)
			return;
		ASSERT(ndx == 1);
	} else {
		/* Check if the requested argument doesn't exist. */
		if (ndx >= n)
			return;

		/* Skip the return type and arguments up to the one requested. */
		dp += width * (ndx + 1);
	}

	if (fbt_type_name(mc, fbt_ctf_word(dp, width), desc->dtargd_native, sizeof(desc->dtargd_native)) > 0)
		desc->dtargd_ndx = ndx;

	return;
}

#ifdef __FreeBSD__
static int
fbt_linker_file_cb(linker_file_t lf, void *arg)
{

	fbt_provide_module(arg, lf);

	return (0);
}
#endif

static void
fbt_load(void)
{

#ifdef __FreeBSD__
	/* Create the /dev/dtrace/fbt entry. */
	fbt_cdev = make_dev(&fbt_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "dtrace/fbt");
#endif
#ifdef __NetBSD__
	(void) module_specific_key_create(&fbt_module_key, fbt_module_dtor);
#endif

	/* Default the probe table size if not specified. */
	if (fbt_probetab_size == 0)
		fbt_probetab_size = FBT_PROBETAB_SIZE;

	/* Choose the hash mask for the probe table. */
	fbt_probetab_mask = fbt_probetab_size - 1;

	/* Allocate memory for the probe table. */
	fbt_probetab =
	    malloc(fbt_probetab_size * sizeof (fbt_probe_t *), M_FBT, M_WAITOK | M_ZERO);

	dtrace_doubletrap_func = fbt_doubletrap;
	dtrace_invop_add(fbt_invop);

	if (dtrace_register("fbt", &fbt_attr, DTRACE_PRIV_USER,
	    NULL, &fbt_pops, NULL, &fbt_id) != 0)
		return;
}


static int
fbt_unload(void)
{
	int error = 0;

	/* De-register the invalid opcode handler. */
	dtrace_invop_remove(fbt_invop);

	dtrace_doubletrap_func = NULL;

	/* De-register this DTrace provider. */
	if ((error = dtrace_unregister(fbt_id)) != 0)
		return (error);

	/* Free the probe table. */
	free(fbt_probetab, M_FBT);
	fbt_probetab = NULL;
	fbt_probetab_mask = 0;

#ifdef __FreeBSD__
	destroy_dev(fbt_cdev);
#endif
#ifdef __NetBSD__
	(void) module_specific_key_delete(fbt_module_key);
#endif
	return (error);
}


static int
dtrace_fbt_modcmd(modcmd_t cmd, void *data)
{
	int bmajor = -1, cmajor = 352;
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		fbt_load();
		return devsw_attach("fbt", NULL, &bmajor,
		    &fbt_cdevsw, &cmajor);
	case MODULE_CMD_FINI:
		error = fbt_unload();
		if (error != 0)
			return error;
		devsw_detach(NULL, &fbt_cdevsw);
		return 0;
	case MODULE_CMD_AUTOUNLOAD:
		return EBUSY;
	default:
		return ENOTTY;
	}
}

static int
fbt_open(dev_t dev, int flags, int mode, struct lwp *l)
{
	return (0);
}

#ifdef __FreeBSD__
SYSINIT(fbt_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, fbt_load, NULL);
SYSUNINIT(fbt_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, fbt_unload, NULL);

DEV_MODULE(fbt, fbt_modevent, NULL);
MODULE_VERSION(fbt, 1);
MODULE_DEPEND(fbt, dtrace, 1, 1, 1);
MODULE_DEPEND(fbt, opensolaris, 1, 1, 1);
#endif
#ifdef __NetBSD__
MODULE(MODULE_CLASS_MISC, dtrace_fbt, "dtrace,zlib");
#endif
