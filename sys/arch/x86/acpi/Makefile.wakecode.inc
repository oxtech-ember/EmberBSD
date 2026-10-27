# $NetBSD: Makefile.wakecode.inc,v 1.9 2014/01/15 22:25:23 joerg Exp $
# FreeBSD: src/sys/i386/acpica/Makefile,v 1.3 2002/05/01 21:54:07 peter Exp

ACPI_WAKECODE_S=	${S}/arch/${MACHINE}/acpi/acpi_wakecode.S
ACPI_GENWAKECODE=	${S}/arch/x86/acpi/genwakecode.sh
ACPI_WAKECODE_LD=	${S}/arch/x86/acpi/wakecode.ldscript
ACPI_GENCMD=		AWK=${TOOL_AWK:Q} \
			HEXDUMP=${TOOL_HEXDUMP:Q} \
			SED=${TOOL_SED:Q} \
			${HOST_SH}

AFLAGS.acpi_wakecode.S= -I${.CURDIR}


acpi_wakecode.h: ${ACPI_WAKECODE_S} ${ACPI_GENWAKECODE} ${ACPI_WAKECODE_LD}
	${_MKTARGET_CREATE}
	${CC} ${AFLAGS.acpi_wakecode.S} -c ${ACPI_WAKECODE_S}
	${LD} -z defs -nostdlib \
	    --oformat binary -e wakeup_16 -T ${ACPI_WAKECODE_LD} \
	    -o acpi_wakecode.bin acpi_wakecode.o
	${NM} -P -t x acpi_wakecode.o > acpi_wakecode.symbols
	${ACPI_GENCMD} ${ACPI_GENWAKECODE} > acpi_wakecode.h.tmp
	mv -f acpi_wakecode.h.tmp acpi_wakecode.h
	rm -f acpi_wakecode.bin acpi_wakecode.o acpi_wakecode.symbols acpi_wakecode.hex

acpi_wakeup.o acpi_wakeup.d: acpi_wakecode.h
