/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_INSTALL_H
#define EMBER_INSTALL_H
#include "slot.h"
void merge_defaults(const char *, const char *, const char *);
void install_file(const char *, const char *, mode_t);
void check_parents(const char *, const char *);
const char *install_relpath(const char *);
const char *install_component(const struct eu_json *);
const struct eu_json *install_array(const struct eu_json *, const char *);
void install_mkdirs(const char *, const char *);
void install_trusted_input(const char *, const char *);
void install_copy(const char *, const char *, const char *, const char *, mode_t);
void install_check_kernel(const char *, const char *, const char *, const char *, int);
void check_loader_inputs(const struct slot *, const char *);
void install_loader_files(const struct slot *, int, const char *, const char *);
#endif
