/* Origin: EmberBSD sysinst wizard frontend, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SYSINST_WIZARD_H
#define SYSINST_WIZARD_H
extern bool wizard_active, wizard_automatic, wizard_installing;
extern bool wizard_custom;
extern bool wizard_initial_back, wizard_expert_requested;
int wizard_initial_choice(const char *, const char * const *, int, int);
void wizard_release_console(void);
int wizard_intro(void);
bool wizard_prepare(struct install_partition_desc *);
bool wizard_configure(void);
void wizard_progress(const char *);
int wizard_disk_choice(const char * const *, int);
bool wizard_disk_is_root(const char *);
void wizard_record_boot_medium(const char *);
int wizard_retry_source(void);
int wizard_check_sets(const char *);
void wizard_limit_sets(void);
void wizard_desktop_sets(bool);
unsigned wizard_optional_omissions(void);
bool wizard_verify_set(distinfo *, const char *);
int wizard_network_source(const char *);
WINDOW *wizard_command_window(void);
#endif
