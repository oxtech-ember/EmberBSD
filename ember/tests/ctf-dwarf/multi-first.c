/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), retain CU-local static symbol ownership. */
struct private_record { long value; };
static volatile struct private_record private_data = { 7 };
long second_value(void);
long first_value(void) { return private_data.value; }
int main(void) { return first_value() + second_value() == 18 ? 0 : 1; }
