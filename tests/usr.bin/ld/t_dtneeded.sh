#	$NetBSD$
#
# Copyright (c) 2026 EmberBSD contributors
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

################################################################################

# libm needs libc through DT_NEEDED, and ld copies DT_NEEDED entries by
# default.  With the LTO plugin, ld loaded libc twice and gave the
# warning of each .gnu.warning section in libc.

atf_test_case ltoplugin
ltoplugin_head() {
	atf_set "descr" "check that an LTO link gives no warnings of" \
	    "a library that comes through DT_NEEDED"
	atf_set "require.progs" "cc" "grep"
}

ltoplugin_body() {
	cat > test.c << EOF
#include <math.h>
double g(double x) { return sin(x); }
EOF
	cc -flto -fPIC -c test.c -o probe.o 2>/dev/null ||
		atf_skip "cc does not support -flto"
	cc -v -flto -shared -o probe.so probe.o -lm >probe.log 2>&1 ||
		atf_skip "cc cannot link with -flto"
	grep -q liblto_plugin probe.log ||
		atf_skip "cc does not use the LTO linker plugin"
	atf_check -s exit:0 -o empty -e empty cc -flto -fPIC -shared \
	    -Wl,--fatal-warnings -Wl,--as-needed -o test.so test.c -lm
}

atf_test_case directref
directref_head() {
	atf_set "descr" "check that a direct reference still gives the" \
	    "link-time warning"
	atf_set "require.progs" "cc"
}

directref_body() {
	cat > test.c << EOF
extern char *gets(char *);
char *h(char *s) { return gets(s); }
EOF
	atf_check -s not-exit:0 -o ignore -e match:gets cc -fPIC -shared \
	    -Wl,--fatal-warnings -o test.so test.c
}

################################################################################

atf_init_test_cases()
{

	atf_add_test_case ltoplugin
	atf_add_test_case directref
}
