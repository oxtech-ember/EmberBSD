# Base OpenLDAP integration

The base build selects `EMBER_OPENLDAP_SOURCE`, the prepared common
`databases/openldap` source from Ports. Its version and feature header come
from that source; generated headers stay in the object tree.

OpenLDAP 2.7 uses one thread-capable library. This integration installs
`libldap.so.7` and keeps `liblber.so.5`. The unversioned `-lldap_r` shared and
static link names point to the corresponding `libldap` library; a consumer
linked this way records `libldap.so.7`. No old `libldap.so.6` or
`libldap_r.so.6` SONAME is provided by the new ABI. Existing binaries using
those SONAMEs need a compatible old runtime or a rebuild.

The old NetBSD-specific public `ldap_gssapi_bind` and `ldap_gssapi_bind_s`
functions and `LDAP_OPT_X_GSSAPI_DO_NOT_FREE_CONTEXT` and
`LDAP_OPT_X_GSSAPI_ALLOW_REMOTE_PRINCIPAL` options are absent from the common
upstream source. The shared LDAP link contract, OpenLDAP server tools,
Postfix, racoon, amd and the OpenSSH LDAP consumers must be rebuilt against
the new library. GSSAPI through SASL is an upstream mechanism; it has not been
validated here as a substitute for those removed interfaces.
