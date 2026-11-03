/*	$NetBSD$	*/

/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Default shared verbosity for clients that do not provide their own.
 * In ordinary standalone clients, the application definition satisfies the
 * reference and this archive member is not extracted.  Crunchgen builds omit
 * each applet's private definition so the common audio library and applet use
 * the same process-local value.
 */

int verbose;
