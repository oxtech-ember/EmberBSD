# $NetBSD: Makefile.inc,v 1.7 2019/01/04 21:22:20 christos Exp $

BINDIR?=	/usr/sbin

USE_FORT?= yes	# network client/server

.if defined(EMBER_WPA_SOURCE)
WPA_DISTDIR?=			${EMBER_WPA_SOURCE}
.if "${WPA_DISTDIR}" != "${EMBER_WPA_SOURCE}"
.error WPA_DISTDIR must match the prepared common WPA source
.endif
.elif make(obj) && empty(.TARGETS:Nobj)
WPA_DISTDIR?=			${.CURDIR}/../../dist
.else
.error Set EMBER_WPA_SOURCE to the prepared common Ports source
.endif
WPA_SUPPLICANT_DISTDIR?=	${WPA_DISTDIR}/wpa_supplicant
HOSTAPD_DISTDIR?=		${WPA_DISTDIR}/hostapd
.if defined(EMBER_WPA_SOURCE) && \
    ("${WPA_SUPPLICANT_DISTDIR}" != "${WPA_DISTDIR}/wpa_supplicant" || \
     "${HOSTAPD_DISTDIR}" != "${WPA_DISTDIR}/hostapd")
.error WPA component directories must match the prepared common WPA source
.endif

COMMON_PATH= \
	${WPA_DISTDIR}/src/ap \
	${WPA_DISTDIR}/src/common \
	${WPA_DISTDIR}/src/crypto \
	${WPA_DISTDIR}/src/eap_common \
	${WPA_DISTDIR}/src/eapol_supp \
	${WPA_DISTDIR}/src/fst \
	${WPA_DISTDIR}/src/l2_packet \
	${WPA_DISTDIR}/src/utils

CPPFLAGS+= \
	-I${WPA_DISTDIR}/src \
	-I${WPA_DISTDIR}/src/ap \
	-I${WPA_DISTDIR}/src/common \
	-I${WPA_DISTDIR}/src/crypto \
	-I${WPA_DISTDIR}/src/l2_packet \
	-I${WPA_DISTDIR}/src/utils

CONFIG_TLS_DEFAULT_CIPHERS = "DEFAULT:!EXP:!LOW"

CPPFLAGS+= -DTLS_DEFAULT_CIPHERS=\"$(CONFIG_TLS_DEFAULT_CIPHERS)\"
CPPFLAGS+=-DCONFIG_CTRL_IFACE -DCONFIG_CTRL_IFACE_UNIX -DCONFIG_ELOOP_KQUEUE

.include <bsd.own.mk>
