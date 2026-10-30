# Origin: EmberBSD optional desktop installation helpers, prepared with AI assistance.
SCRIPTS=	ember-desktop-install ember-desktop
.include <bsd.own.mk>
# The runtime helper installs the GCC 16 base runtime, which only the GCC
# profile has; sysinst calls it only with EMBER_BASE_RUNTIME_GCC16.
.if defined(EMBER_EXTERNAL_GCC_RUNTIME)
SCRIPTS+=	ember-runtime-install
.endif
SCRIPTSDIR=	/usr/libexec
SCRIPTSDIR_ember-desktop=	/usr/bin
MAN=
.include <bsd.prog.mk>
