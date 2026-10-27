#!/bin/sh
# $NetBSD: genwakecode.sh,v 1.3 2009/01/18 13:55:31 hans Exp $

set -eu

AWK=${AWK:=awk}
HEXDUMP=${HEXDUMP:=hexdump}
SED=${SED:=sed}

P='
$1 == "wakeup_16" {
	if ($2 != "T" || $3 !~ /^0+$/) exit 1;
	entry = 1;
}
$1 ~ /^WAKEUP_/ {
	if ($2 != "T" || $3 !~ /^[[:xdigit:]]+$/) exit 1;
	count++;
	printf("#define\t%s%s\t0x%s\n", $1, length($1) < 16 ? "\t" : "", $3);
}
END { if (!entry || !count) exit 1; }
'
${AWK} "$P" < acpi_wakecode.symbols

echo 
echo 'static const unsigned char wakecode[] = {';
${HEXDUMP} -v -e '"\t" 8/1 "0x%02x, " "\n"' < acpi_wakecode.bin > acpi_wakecode.hex
${SED} 's/0x  /0x00/g' < acpi_wakecode.hex
echo '};'

exit 0
