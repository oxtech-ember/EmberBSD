#!/bin/sh
# Origin: EmberBSD (AI-assisted). Config device bases must not end in a digit.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail=0
while IFS= read -r line; do
	name=${line#*device}
	name=${name%%:*}
	name=$(printf '%s' "$name" | awk '{print $1}')
	case "$name" in
	rtwce|rtw88_pci) ;;
	*[0-9])
		echo "FAIL: config device base '$name' ends in a digit" >&2
		fail=1
		;;
	esac
done <<EOF
$(grep -E '^[[:space:]]*device[[:space:]]+rtw' "$src/sys/conf/files" "$src/sys/dev/pci/files.pci")
EOF
if ! grep -q '^[[:space:]]*device[[:space:]]*rtwce' "$src/sys/conf/files"; then
	echo "FAIL: expected device rtwce in sys/conf/files" >&2
	fail=1
fi
if ! grep -q 'attach[[:space:]]*rtwce[[:space:]]*at[[:space:]]*pci' "$src/sys/dev/pci/files.pci"; then
	echo "FAIL: expected attach rtwce at pci" >&2
	fail=1
fi
if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "rtw88-config-name: PASS"
