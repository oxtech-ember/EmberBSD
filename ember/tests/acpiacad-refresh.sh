#!/bin/sh
# Execute the acpiacad(4) status read and envsys refresh from the production
# driver with a controlled _PSR provider and clock.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpiacad-refresh.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/acpi/acpi_acad.c}

extract() {
	awk '/^#define[[:space:]]+ACPIACAD_/ { print }
/^struct acpiacad_softc \{/ { copy = 1 }
copy { print }
copy && /^};/ { copy = 0 }' "$1" > "$2/acpiacad-types.h"
	awk '/^acpiacad_(get_status|refresh)\(/ { print "static void"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$1" > "$2/acpiacad-functions.h"
	grep -q '^acpiacad_get_status(' "$2/acpiacad-functions.h"
	grep -q '^acpiacad_refresh(' "$2/acpiacad-functions.h"
}

build() {
	${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror \
	    -Wno-unused-parameter -I"$1" \
	    "$src/ember/tools/acpiacad-refresh-contract.c" ${LDFLAGS:-} \
	    -o "$1/check"
}

extract "$input" "$work"
build "$work"
${TEST_RUNNER:-} "$work/check"

# Deliberate faults in the extracted functions: the contract must catch each.
[ "${ACPIACAD_MUTATIONS:-1}" = 1 ] || exit 0
caught=0
total=0
mutate() {
	total=$((total + 1))
	dir=$work/mutant$total
	mkdir "$dir"
	cp "$work/acpiacad-types.h" "$dir/"
	sed -e "$1" "$work/acpiacad-functions.h" > "$dir/acpiacad-functions.h"
	if cmp -s "$work/acpiacad-functions.h" "$dir/acpiacad-functions.h"; then
		echo "acpiacad-refresh: mutation $total changed nothing: $1" >&2
		exit 1
	fi
	if ! build "$dir" 2> "$dir/build.log"; then
		caught=$((caught + 1))
	elif ! "$dir/check" > /dev/null 2>&1; then
		caught=$((caught + 1))
	else
		echo "acpiacad-refresh: mutation $total survived: $1" >&2
	fi
}
mutate 's/<= ACPIACAD_REFRESH_SECS/< ACPIACAD_REFRESH_SECS/'
mutate 's/<= ACPIACAD_REFRESH_SECS/<= ACPIACAD_REFRESH_SECS + 1/'
mutate 's/sc->sc_sampled &&/!sc->sc_sampled ||/'
mutate '/sc->sc_stamp = time_uptime;/d'
mutate '/sc->sc_sampled = true;/d'
mutate 's/if (!fresh)/if (fresh)/'
echo "acpiacad-refresh: $caught of $total mutations caught"
[ "$caught" -eq "$total" ]
