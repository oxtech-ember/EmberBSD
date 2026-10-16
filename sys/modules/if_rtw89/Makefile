#	$NetBSD$

.include "../Makefile.inc"

.PATH:	${S}/dev/pci/rtw89

KMOD=	if_rtw89
IOCONF=	if_rtw89.ioconf

# NetBSD glue and Linux API emulation.
SRCS=	if_rtw89.c rtw89_compat.c rtw89_bridge.c

# Sources derived from the Linux rtw89 driver (GPL-2.0 OR BSD-3-Clause).
RTW89_LINUX_SRCS= \
	rtw89_core.c rtw89_mac.c rtw89_mac_be.c rtw89_phy.c rtw89_phy_be.c \
	rtw89_fw.c rtw89_pci.c rtw89_efuse.c rtw89_cam.c rtw89_chan.c \
	rtw89_coex.c rtw89_ps.c rtw89_ser.c rtw89_sar.c rtw89_regd.c \
	rtw89_acpi.c rtw89_mac80211.c rtw89_8852be.c rtw8852b.c \
	rtw8852b_rfk.c rtw8852b_rfk_table.c rtw8852b_table.c
SRCS+=	${RTW89_LINUX_SRCS}

CPPFLAGS+=	-I${S}/dev/pci/rtw89
.for f in ${RTW89_LINUX_SRCS} rtw89_bridge.c rtw89_compat.c
CPPFLAGS.${f}+=	-I${S}/dev/pci/rtw89/compat -DCONFIG_RTW89_DEBUGMSG
COPTS.${f}+=	-Wno-shadow -Wno-cast-qual -Wno-missing-prototypes \
		-Wno-missing-field-initializers -Wno-unused-function \
		-Wno-unused-variable -Wno-unused-but-set-variable \
		-Wno-strict-prototypes -Wno-pointer-arith -Wno-sign-compare \
		-Wno-implicit-fallthrough -Wno-address-of-packed-member \
		-Wno-type-limits -Wno-format-zero-length -Wno-array-bounds \
		-Wno-old-style-declaration -Wno-int-in-bool-context -Wno-discarded-qualifiers
.endfor

WARNS=	3

.include <bsd.kmodule.mk>
