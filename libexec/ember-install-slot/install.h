/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_INSTALL_H
#define EMBER_INSTALL_H
#include "slot.h"
void merge_defaults(const char *, const char *, const char *);
void install_file(const char *, const char *, mode_t);
void check_parents(const char *, const char *);
#endif
