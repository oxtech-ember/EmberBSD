/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_CONTROL_H
#define EMBER_UPDATE_CONTROL_H
#include <stdbool.h>
#include <signal.h>
#include "catalog.h"
#include "local.h"

#define DATABASE "/var/db/ember-update"
#define STATE DATABASE "/state.json"
#define ATTEMPT DATABASE "/attempt.json"
#define RECEIPT DATABASE "/os-release.json"
#define LOCAL DATABASE "/local-kernel.json"
#define INSTALLED DATABASE "/installed.json"
#define INCOMING DATABASE "/incoming"
#define PROFILE "/etc/ember-update-profile.json"
#define ANCHOR "/etc/openssl/emberbsd-release-root.pem"
#define ORIGIN "https://emberbsd-pkg.oxteam.me"
#define HEALTH "/etc/ember-os-health"
#define BOOT "/usr/libexec/ember-os-slot"
#define INSTALL "/usr/libexec/ember-install-slot"
#define PLAN "emberbsd.install-plan.v1"
#define CUSTOM "emberbsd.local-kernel.v1"

extern struct eu_json *ui;
extern volatile sig_atomic_t stopping;
struct policy {
	char *channel, *window;
	bool packages, os;
};
struct hardware {
	struct eu_json *profile, *receipt;
	const struct eu_json *target;
	const char *name, *commit;
	int64_t generation;
	bool custom, single;
};
struct slots {
	char running[2], confirmed[2], next[2], prepared[2], digest[65];
	bool trial;
};
const char *text_field(const struct eu_json *, const char *);
int64_t int_field(const struct eu_json *, const char *);
struct eu_json *read_record(const char *, bool);
struct eu_json *load_ui(void);
void report(const char *, const char *);
void history(const char *, const char *);
void fail(const char *) __attribute__((__noreturn__));
void checkpoint(void);
void trial_health(void);
void trusted(const char *, bool);
void read_policy(struct policy *);
bool window_open(const char *, time_t);
void load_hardware(struct hardware *);
void slot_status(struct slots *);
bool reconcile(const struct slots *, const struct hardware *, bool);
void prepare(const struct slots *, const char *, const char *, bool, int64_t, bool);
void release_cycle(const struct slots *, const struct hardware *, const struct policy *,
		   bool, bool);
int file_sha(const char *, char[65]);
#endif
