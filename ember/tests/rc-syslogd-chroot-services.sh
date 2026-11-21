#!/bin/sh
# Exercise the syslogd rc.d chroot-service selection from the actual script.

set -eu

src=${1:-$(pwd)}
case "$src" in
/*) ;;
*) src=$(cd "$src" && pwd) ;;
esac
tmp=${TMPDIR:-/tmp}/rc-syslogd-chroot-$$
umask 077
mkdir "$tmp"
trap 'rm -rf "$tmp"' 0 HUP INT TERM
mkdir "$tmp/rc.d" "$tmp/bin"
: > "$tmp/rc.d/named"
: > "$tmp/rc.d/ntpd"

awk '
$0 == "syslogd_precmd()" { copying = 1 }
copying { print }
copying && $0 == "}" { exit }
' "$src/etc/rc.d/syslogd" > "$tmp/function"
test -s "$tmp/function"

cat > "$tmp/bin/rcorder" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$RCORDER_TRACE"
printf '%s\n' "$RCORDER_OUTPUT"
EOF
chmod 0555 "$tmp/bin/rcorder"

cat > "$tmp/harness" <<'EOF'
#!/bin/sh
set -eu
tmp=$1
. "$tmp/function"

# The real script maintains /dev/log. Keep this host-side contract isolated.
ln() { return 0; }
warn() { :; }

load_rc_config()
{
	name=service-$1
	rc_flags='-f from-service-config'
	case $1 in
	named)
		named=${TEST_NAMED_ENABLED:-NO}
		named_chrootdir=${TEST_NAMED_CHROOT:-}
		;;
	ntpd)
		ntpd=${TEST_NTPD_ENABLED:-NO}
		ntpd_chrootdir=${TEST_NTPD_CHROOT:-}
		;;
	*) return 0 ;;
	esac
}

checkyesno()
{
	_status=0
	checkyesnox "$1" || _status=$?
	case $_status in
	0) return 0 ;;
	1) return 1 ;;
	*) warn "invalid boolean $1"; return 1 ;;
	esac
}

checkyesnox()
{
	case $1 in
	syslogd_chroot_services) _value=${syslogd_chroot_services:-} ;;
	named) _value=$named ;;
	ntpd) _value=$ntpd ;;
	*) return 2 ;;
	esac
	case $_value in
	YES|yes|TRUE|true|ON|on|1) return 0 ;;
	NO|no|FALSE|false|OFF|off|0) return 1 ;;
	*) return 2 ;;
	esac
}

check_sockets()
{
	printf '%s\n' "$1" > "$tmp/expected"
	cmp "$tmp/expected" "$_sockfile"
}

rc_directories=$tmp/rc.d
_sockfile=$tmp/sockets
RCORDER_TRACE=$tmp/rcorder.trace
export RCORDER_TRACE
PATH=$tmp/bin:$PATH
export PATH

# The default discovery path remains available to full systems and manual
# restarts that do not inherit the profile's explicit closed service set.
unset syslogd_chroot_services
RCORDER_OUTPUT=$tmp/rc.d/named
export RCORDER_OUTPUT
TEST_NAMED_ENABLED=YES
TEST_NAMED_CHROOT=/var/chroot/named
TEST_NTPD_ENABLED=NO
TEST_NTPD_CHROOT=/var/chroot/ntpd
export TEST_NAMED_ENABLED TEST_NAMED_CHROOT TEST_NTPD_ENABLED TEST_NTPD_CHROOT
rc_flags='-p /var/run/extra'
name=syslogd
syslogd_precmd
test -s "$RCORDER_TRACE"
check_sockets '/var/chroot/named/var/run/log
/var/run/log'
case $rc_flags in "-P $_sockfile -p /var/run/extra") ;; *) exit 1 ;; esac
test "$name" = syslogd

# The profile opt-out avoids rcorder but preserves caller-provided -p sockets.
: > "$RCORDER_TRACE"
syslogd_chroot_services=NO
rc_flags='-p /var/run/extra'
name=syslogd
syslogd_precmd
test ! -s "$RCORDER_TRACE"
test ! -s "$_sockfile"
test "$rc_flags" = '-p /var/run/extra'

# Invalid booleans warn through checkyesno and preserve the safe auto scan.
: > "$RCORDER_TRACE"
syslogd_chroot_services=invalid
RCORDER_OUTPUT=$tmp/rc.d/named
rc_flags='-p /var/run/extra'
name=syslogd
syslogd_precmd
test -s "$RCORDER_TRACE"
check_sockets '/var/chroot/named/var/run/log
/var/run/log'
test "$name" = syslogd

# Discovery keeps service configuration in a subshell, so a service cannot
# overwrite syslogd's own rc flags or identity. Repeated starts see changes.
unset syslogd_chroot_services
RCORDER_OUTPUT="$tmp/rc.d/named $tmp/rc.d/ntpd"
export RCORDER_OUTPUT
TEST_NAMED_ENABLED=YES
TEST_NAMED_CHROOT=/var/chroot/named
TEST_NTPD_ENABLED=YES
TEST_NTPD_CHROOT=/var/chroot/ntpd
rc_flags='-p /var/run/extra'
name=syslogd
syslogd_precmd
test -s "$RCORDER_TRACE"
check_sockets '/var/chroot/named/var/run/log
/var/chroot/ntpd/var/run/log
/var/run/log'
case $rc_flags in "-P $_sockfile -p /var/run/extra") ;; *) exit 1 ;; esac
test "$name" = syslogd

: > "$RCORDER_TRACE"
TEST_NAMED_ENABLED=NO
TEST_NTPD_CHROOT=/var/chroot/ntpd-updated
rc_flags='-p /var/run/extra'
name=syslogd
syslogd_precmd
test -s "$RCORDER_TRACE"
check_sockets '/var/chroot/ntpd-updated/var/run/log
/var/run/log'
test "$name" = syslogd

echo 'syslogd chroot socket selection: PASS'
EOF
chmod 0555 "$tmp/harness"

RCORDER_TRACE="$tmp/rcorder.trace" \
RCORDER_OUTPUT="$tmp/rc.d/named" \
"$tmp/harness" "$tmp"
