# $NetBSD: t_makefs.sh,v 1.1 2024/10/18 23:22:52 christos Exp $
#
# Copyright (c) 2024 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas
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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#


# replace
#
atf_test_case replace replace_cleanup

replace_head() {
	atf_require_prog makefs
	atf_set "descr" "Test replacing a single file (PR/58759)"
}

replace_body() {
	export PATH=/sbin:/bin:/usr/sbin:/usr/bin
	mkdir -p 1 2/1
	echo foo > 1/foo
	echo bar > 2/1/foo
	makefs -r -d 0xf0 -s 1m -t ffs foo.img 1 2/1
	dump 0f foo.dump -F foo.img
	restore -rf foo.dump
	if [ ! -f foo ]; then
		atf_fail "file foo does not exist"
	fi
	x=$(cat foo)
	if [ "$x" != "bar" ]; then
		atf_fail "file foo does not contain bar, but $x"
	fi
	atf_pass
}

replace_cleanup() {
	rm -fr 1 2 foo foo.dump foo.img
}

atf_test_case search_only cleanup
search_only_head() {
	atf_set "descr" "Build selected entries from a search-only directory"
	atf_set "require.user" "unprivileged"
	atf_set "require.progs" "makefs dump restore"
}

search_only_body() {
	mkdir -p src/empty src/hidden overlay/hidden result
	echo retained > src/hidden/selected
	echo excluded > src/hidden/other
	echo overlay > overlay/hidden/second
	chmod 0000 src/empty
	chmod 0111 src/hidden
	cat > spec <<'EOF'
. type=dir uid=0 gid=0 mode=0755
./empty type=dir uid=0 gid=0 mode=0000
./hidden type=dir uid=0 gid=0 mode=0111
./hidden/selected type=file uid=0 gid=0 mode=0644
./hidden/second type=file uid=0 gid=0 mode=0644
EOF
	atf_check -s exit:0 -o ignore -e ignore makefs -r -x -F spec \
	    -t ffs -s 2m image src overlay
	dump 0f image.dump -F image || atf_fail "cannot read the image"
	(cd result && restore -rf ../image.dump)
	atf_check -o inline:"0\n" stat -f '%Lp' result/empty
	atf_check -o inline:"111\n" stat -f '%Lp' result/hidden
	atf_check -o inline:"retained\n" cat result/hidden/selected
	atf_check -o inline:"overlay\n" cat result/hidden/second
	atf_check test ! -e result/hidden/other
	atf_check -s not-exit:0 -o ignore -e match:"Can't opendir" \
	    makefs -F spec -t ffs -s 2m all.img src
	atf_check -s not-exit:0 -o ignore -e match:"Can't opendir" \
	    makefs -x -x -F spec -t ffs -s 2m strict.img src
	chmod 0000 src/hidden/selected
	atf_check -s not-exit:0 -o ignore -e ignore makefs -r -x -F spec \
	    -t ffs -s 2m unreadable.img src overlay
}

search_only_cleanup() {
	chmod 0700 src/empty src/hidden result/empty result/hidden 2>/dev/null || true
}

atf_init_test_cases() {
	atf_add_test_case replace
	atf_add_test_case search_only
}
