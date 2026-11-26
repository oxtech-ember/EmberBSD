/* SPDX-License-Identifier: BSD-2-Clause */
/* Files consumed by firmware before the kernel opens its FFS root. */
#include "install.h"

void
check_loader_inputs(const struct slot *s, const char *dir)
{
	if (s->efi)
		return;	/* efiboot reads the ELF already checked on FFS. */
	install_trusted_input(dir, "netbsd.img");
	for (const struct eu_json *p = install_array(s->profile, "trees"); p; p = p->next)
		install_trusted_input(dir, install_component(p));
}

void
install_loader_files(const struct slot *s, int dest, const char *source, const char *commit)
{
	char b[MAXPATHLEN];
	const char *boot = dest ? "/boot/os-b" : "/boot";
	const char *src = source;
	const struct eu_json *p;
	if (s->efi)
		return;
	if (dest)
		install_mkdirs("/boot", "os-b");
	const char *trees = install_relpath(field(s->profile, "tree_directory"));
	install_mkdirs(boot, trees);
	if (!source)
		src = s->running ? "/boot/os-b" : "/boot";
	install_check_kernel(src, "netbsd.img", commit, field(s->profile, "kernel"), 1);
	install_copy(src, "netbsd.img", boot, "netbsd.img", 0644, INSTALL_INACTIVE);
	for (p = install_array(s->profile, "trees"); p; p = p->next) {
		const char *tree = install_component(p);
		slot_path(b, sizeof(b), trees, tree);
		install_copy(src, source ? tree : b, boot, b, 0644, INSTALL_INACTIVE);
	}
}
