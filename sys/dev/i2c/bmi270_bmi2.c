/*
 * Copyright (c) 2026 The EmberBSD Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */

/* Prepared upstream BMI2 source is installed by the common Ports package. */
#if !defined(__has_include) || !__has_include(<bmi2.c>)
#error "Missing prepared BMI270 source; install devel/bmi270-sensorapi in EXTERNAL_TOOLCHAIN"
#endif
#include <bmi2.c>
