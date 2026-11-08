#!/bin/sh
# Origin: EmberBSD (AI-assisted), first-boot account and SSH regressions.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
handler=${1:-$here/../etc/rc.d/rpi_preseed}
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-preseed-test.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/run.sh" <<'EOF'
#!/bin/sh
# Run the production handler with real temporary files. Replace only commands
# that would change the host's accounts, ownership or running configuration.
_rc_subr_loaded=:
load_rc_config() { :; }
checkyesno() { eval '[ "${'"$1"':-NO}" = YES ]'; }
run_rc_command() { rpi_preseed_start; }
id() { [ -f "$rpi_preseed_root/account-created" ]; }
useradd() {
	[ "$failure" != useradd ] || return 1
	echo 'embertest:*:1000:100:Test:/srv/embertest:/bin/sh' >> "$rpi_preseed_root/etc/passwd"
	touch "$rpi_preseed_root/account-created"
}
usermod() {
	[ "$failure" != usermod ] || return 1
	printf '%s\n' "$*" > "$rpi_preseed_root/password-command"
}
chown() { [ "$failure" != chown ]; }
hostname() { return 1; }
. "$handler"
EOF
passed=0
fixture()
{
	root=$work/case-$passed
	mkdir -p "$root/etc/ssh" "$root/var/log"
	printf 'sshd=YES\n' > "$root/etc/rc.conf"
	: > "$root/etc/passwd"
	cat > "$root/etc/ssh/sshd_config" <<'EOF'
# Preserve the base's independent authentication and root-login policy.
PermitRootLogin prohibit-password
PermitEmptyPasswords no
UsePAM yes
 PasswordAuthentication yes
ChallengeResponseAuthentication yes
Match=User legacy
    PasswordAuthentication yes
EOF
	cat > "$root/preseed.toml" <<'EOF'
[user]
name = "embertest"
groups = ["wheel"]
password = "$1$fixture$not-a-real-password"
[ssh]
enabled = true
password_authentication = false
authorized_keys = ["ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA fixture"]
EOF
}
invoke()
{
	status=0
	env handler="$handler" failure="$1" rpi_preseed_root="$root" \
	    rpi_preseed_file="$root/preseed.toml" sh "$work/run.sh" > "$root/log" 2>&1 || status=$?
}
pass() { passed=$((passed + 1)); printf 'PASS: %s\n' "$1"; }
require() { "$@" || { cat "$root/log" >&2; echo "FAIL: $*" >&2; exit 1; }; }
for failure in useradd usermod chown; do
	fixture
	invoke "$failure"
	require test "$status" -ne 0
	require test -f "$root/preseed.toml"
	! grep -q 'done;' "$root/log"
	# Retry after the cause is repaired; no duplicate keys or lost selection.
	invoke none
	require test "$status" -eq 0
	require test ! -e "$root/preseed.toml"
	require test "$(wc -l < "$root/srv/embertest/.ssh/authorized_keys" | tr -d ' ')" = 1
	pass "$failure failure retained preseed; retry completed"
done
fixture
invoke none
require test "$status" -eq 0
require grep -q '^PasswordAuthentication no$' "$root/etc/ssh/sshd_config"
require grep -q '^KbdInteractiveAuthentication no$' "$root/etc/ssh/sshd_config"
require grep -q '^UsePAM yes$' "$root/etc/ssh/sshd_config"
require grep -q '^PermitRootLogin prohibit-password$' "$root/etc/ssh/sshd_config"
require grep -q '^    PasswordAuthentication yes$' "$root/etc/ssh/sshd_config"
! grep -q '^ChallengeResponseAuthentication ' "$root/etc/ssh/sshd_config"
require test -f "$root/srv/embertest/.ssh/authorized_keys"
require test ! -e "$root/home/embertest"
require test "$(find "$root/srv/embertest/.ssh" -type d -perm 700 | wc -l | tr -d ' ')" = 1
require test "$(find "$root/srv/embertest/.ssh" -type f -perm 600 | wc -l | tr -d ' ')" = 1
require test -z "$(find "$root" -name 'ssh_host_*' -print)"
pass 'selected key, account home, permissions and global key-only policy; Match and host keys preserved'
fixture
printf 'PasswordAuthentication no\n' > "$root/etc/ssh/sshd_config"
sed 's/password_authentication = false/password_authentication = true/' "$root/preseed.toml" > "$root/new"
mv "$root/new" "$root/preseed.toml"
invoke none
require test "$status" -eq 0
require grep -q '^PasswordAuthentication yes$' "$root/etc/ssh/sshd_config"
require grep -q '^KbdInteractiveAuthentication yes$' "$root/etc/ssh/sshd_config"
require grep -q '^-p \$1\$fixture\$not-a-real-password embertest$' "$root/password-command"
pass 'selected supported password and directive-only sshd_config'
fixture
sed 's/\$1\$fixture/\$y\$fixture/' "$root/preseed.toml" > "$root/new"
mv "$root/new" "$root/preseed.toml"
invoke none
require test "$status" -eq 0
require test ! -e "$root/password-command"
require test -f "$root/srv/embertest/.ssh/authorized_keys"
require grep -q 'crypt(3) cannot check its hash' "$root/log"
pass 'existing unsupported-hash warning and selected key preserved'
printf 'Passed %s preseed cases. No host accounts or services changed.\n' "$passed"
