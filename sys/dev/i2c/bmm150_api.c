/*
 * Copyright (c) 2026 The EmberBSD Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */

/* The source owner installs this patched upstream file from Ports. */
#if !defined(__has_include) || !__has_include(<bmm150.c>)
#error "Missing prepared BMM150 source; install devel/bmm150-sensorapi in EXTERNAL_TOOLCHAIN"
#endif
#include <bmm150.c>
