#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native Wi-Fi CI contract sharding, AI-assisted.
# Print the contract runners owned by one CI shard, one per line.
# The heaviest Memcheck/analyzer runners own a shard each; the rest
# alternate between two shards. rtw88-workflow-contract.py checks that
# every runner belongs to exactly one shard of the workflow matrix.
set -eu
LC_ALL=C; export LC_ALL
[ "$#" -eq 1 ] || { echo "usage: $0 shard" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
heavy="rfk enable routing signal"
case "$1" in
rfk|enable|routing|signal)
	echo "ember/tools/rtw88-$1-contract.sh"
	;;
rest-0|rest-1)
	i=0
	for path in "$tools"/rtw*-contract.sh "$tools"/net80211-ht20-contract.sh \
	    "$tools"/net80211-rxba-contract.sh; do
		name=$(basename "$path")
		skip=0
		for h in $heavy; do
			[ "$name" != "rtw88-$h-contract.sh" ] || skip=1
		done
		[ "$skip" = 0 ] || continue
		[ "$((i % 2))" != "${1#rest-}" ] || echo "ember/tools/$name"
		i=$((i + 1))
	done
	;;
*)
	echo "unknown shard: $1" >&2
	exit 2
	;;
esac
