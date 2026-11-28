#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the installed stage script with ordinary directories in place of mounts.
set -eu

if [ -n "${EMBER_FFS_CLEANUP_CASE:-}" ]; then
	stage=$0
	scenario=$EMBER_FFS_CLEANUP_CASE
	fixture=$EMBER_FFS_CLEANUP_DIR
	parent=$$
	fssconfig() {
		case "$1" in
		-l) echo 'fss7: not in use'; return ;;
		-x)
			if [ "$scenario" = rejected ]; then
				echo foreign >"$fixture/fss"
				return 1
			fi
			echo owned >"$fixture/fss" ;;
		-u)
			echo detach >>"$fixture/detach"
			test "$(cat "$fixture/fss")" = owned || return 1
			rm "$fixture/fss" ;;
		esac
		case "$scenario:$1" in
		acquire:-x|release:-u)
			# The ioctl has succeeded. Signal the caller and the utility itself.
			/bin/sh -c 'kill -TERM "$1"; kill -TERM "$$"; echo completed' utility "$parent" \
			    >"$fixture/completed" ;;
		esac
	}
	newfs() { :; }
	fsck_ffs() { :; }
	mount() (
		for point do :; done
		leaf=${point##*/}
		printf '%s-data\n' "$leaf" >"$point/sentinel"
		case "$scenario:$leaf" in
		snapshot:snapshot|root:root|full:root|busy:root|foreign:root|parent:root|layered:root)
			kill -TERM "$parent" ;;
		esac
	)
	df() {
		test "$1:$2:$3" = '-PM:-t:ffs' || return 1
		point=$4
		case "${point##*/}" in
		snapshot) from=/dev/fss7 ;;
		root) from=/dev/dk7 ;;
		*) return 1 ;;
		esac
		case "$scenario:${point##*/}" in
		foreign:root) from=/dev/another ;;
		parent:root) point=/ ;;
		esac
		echo 'Filesystem 512-blocks Used Available Capacity Mounted on'
		printf '%s 100 10 90 10%% %s\n' "$from" "$point"
	}
	umount() {
		leaf=${1##*/}
		echo "$leaf" >>"$fixture/umounts"
		case "$scenario:$leaf" in
		busy:root) return 1 ;;
		layered:root) return 0 ;;
		esac
		mv "$1/sentinel" "$fixture/$leaf.released"
	}
	method=packages
	case "$scenario" in full|release) method=full ;; esac
	set -- "$fixture/work" /dev/dk7 "$method" prepare B "$fixture/plan"
	. "$stage"
	exit 2
fi

stage=$(realpath "$1")
testfile=$(realpath "$0")
work=$(mktemp -d)
# This test creates only ordinary directories and short sentinel files.
trap 'rm -rf "$work"' EXIT
for scenario in root full snapshot busy foreign parent layered acquire rejected release; do
	fixture=$work/$scenario
	mkdir -p "$fixture/work"
	result=0
	EMBER_FFS_CLEANUP_CASE=$scenario EMBER_FFS_CLEANUP_DIR=$fixture \
	    /bin/sh -c '. "$1"' "$stage" "$testfile" || result=$?
	test "$result" = 1
	if [ "$scenario" = rejected ]; then
		test "$(cat "$fixture/fss")" = foreign
		test ! -e "$fixture/detach"
		test ! -e "$fixture/work"
		continue
	fi
	test ! -e "$fixture/fss"
	test "$(cat "$fixture/detach")" = detach
	case "$scenario" in acquire|release) test "$(cat "$fixture/completed")" = completed ;; esac
	if [ "$scenario" = acquire ]; then
		test ! -e "$fixture/work"
		continue
	fi
	test "$(cat "$fixture/snapshot.released")" = snapshot-data
	case "$scenario" in
	snapshot|release) test ! -e "$fixture/work" ;;
	root|full)
		test ! -e "$fixture/work"
		test "$(cat "$fixture/root.released")" = root-data ;;
	*)
		test "$(cat "$fixture/work/root/sentinel")" = root-data
		case "$scenario" in
		foreign|parent) test "$(cat "$fixture/umounts")" = snapshot ;;
		busy|layered) grep -Fxq root "$fixture/umounts" ;;
		esac ;;
	esac
done
echo 'Verified interrupt cleanup for owned, busy and foreign mounts.'
