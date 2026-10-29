# Native IPFilter source

The native userland and kernel IPFilter trees originate in the NetBSD import
of IPFilter 5.1.2 and remain one maintained implementation. The version and
kernel interface are defined by `sys/external/bsd/ipf/netinet/ipl.h`.

The ANSI function definitions in the userland library retain the parameter
types and ordering of the original definitions and declarations. They were
compared with the FreeBSD `sbin/ipf/libipf` sources at commit
`5e66bf0df3104e212c1b24ff65284b6d1a694c59`; no FreeBSD-specific runtime
behavior was imported. Original source copyright and licence notices remain
in place. EmberBSD changes were AI-assisted.
