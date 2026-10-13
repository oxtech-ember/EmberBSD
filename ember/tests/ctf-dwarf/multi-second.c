/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), same local names with a distinct layout. */
struct private_record { int marker; long value; };
static volatile struct private_record private_data = { 3, 11 };
long second_value(void) { return private_data.value; }
