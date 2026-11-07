# $NetBSD$
# SPDX-License-Identifier: BSD-2-Clause

atf_test_case littlefs_image
littlefs_image_head() {
	atf_set "descr" "makefs creates an upstream littlefs image with BSD metadata"
	atf_set "require.progs" "makefs h_littlefs_check"
}
littlefs_image_body() {
	mkdir -p tree/sub
	printf 'hello littlefs\n' > tree/hello
	printf 'nested\n' > tree/sub/file
	ln -s hello tree/link
	cat > device.mtree <<EOF
.	type=dir
device	type=char device=netbsd,199,703710 mode=0600 uid=42 gid=43
EOF
	atf_check -s exit:0 makefs -s 1m -t littlefs -F device.mtree image.lfs tree
	atf_check -s exit:0 h_littlefs_check image.lfs
	atf_check -s not-exit:0 makefs -s 1048575 -t littlefs bad-size.lfs tree
	# The complete image span must fit in off_t before makefs opens the output.
	atf_check -s not-exit:0 -e match:'littlefs image offset range exceeds off_t' \
	    makefs -s 1m -t littlefs -O 9223372036854771712 overflow.lfs tree
	test ! -e overflow.lfs || atf_fail "overflow image was created"
	mkdir hardlinks
	echo hardlink > hardlinks/a
	ln hardlinks/a hardlinks/b
	atf_check -s not-exit:0 -e match:EOPNOTSUPP makefs -s 1m \
	    -t littlefs hardlinks.lfs hardlinks
	mkdir reserved
	mkdir reserved/.littlefs-tmp
	atf_check -s not-exit:0 -e match:reserved makefs -s 1m \
	    -t littlefs reserved.lfs reserved
}
littlefs_image_cleanup() {
	rm -rf tree hardlinks reserved image.lfs bad-size.lfs hardlinks.lfs \
	    reserved.lfs overflow.lfs
	rm -f device.mtree
}

atf_init_test_cases() {
	atf_add_test_case littlefs_image
}
