#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the same catalog projection and package selection used by the CLI.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$root"
cc=${CC:-cc}
case $(uname -s) in Darwin) strip=-Wl,-dead_strip ;; *) strip=-Wl,--gc-sections ;; esac
inc="-Icommon/lib/libemberupdate -Iexternal/bsd/ntp/dist/libjsmn -Iusr.sbin/ember-update -DJSMN_PARENT_LINKS"
"$cc" -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${CPPFLAGS:-} $inc \
    common/lib/libemberupdate/json.c external/bsd/ntp/dist/libjsmn/jsmn.c \
    usr.sbin/ember-update/catalog.c usr.sbin/ember-update/local.c usr.sbin/ember-update/state.c \
    usr.sbin/ember-update/package.c usr.sbin/ember-update/desktop.c usr.sbin/ember-update/store.c \
    usr.sbin/ember-update/tests/store.c ${LDFLAGS:-} "$strip" -o "$tmp/store"
db=$tmp/pkgdb
prefix=$tmp/prefix
apps=$prefix/share/applications
mkdir -p "$db" "$apps" "$prefix/bin"
"$tmp/store" plan "$db" wrong
"$tmp/store" plan "$db" new
for p in app-1.0 libdep-1.0; do
	mkdir "$db/$p"
	printf '@cwd %s\n@name %s\n' "$prefix" "$p" >"$db/$p/+CONTENTS"
done
echo automatic=YES >"$db/app-1.0/+INSTALLED_INFO"
"$tmp/store" plan "$db" promote
echo automatic=NO >"$db/app-1.0/+INSTALLED_INFO"
"$tmp/store" plan "$db" manual
printf '#!/bin/sh\n' >"$prefix/bin/application"
chmod 0755 "$prefix/bin/application"
cat >"$apps/app.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Viewer
Name[ru]=Обзор
Exec=application "two words" %f
TryExec=application
Categories=Utility;Graphics;Utility;
OnlyShowIn=KDE;
Icon=/etc/passwd
DESKTOP
cp "$apps/app.desktop" "$apps/hidden.desktop"
echo NoDisplay=true >>"$apps/hidden.desktop"
cp "$apps/app.desktop" "$apps/unlisted.desktop"
cp "$apps/app.desktop" "$apps/ignored.desktop"
cp "$apps/app.desktop" "$tmp/escaped.desktop"
ln -s "$tmp/escaped.desktop" "$apps/outside.desktop"
cp "$apps/app.desktop" "$apps/bad.desktop"
echo 'Exec=/bin/sh' >>"$apps/bad.desktop"
cat >>"$db/app-1.0/+CONTENTS" <<'PLIST'
share/applications/app.desktop
share/applications/app.desktop
share/applications/hidden.desktop
share/applications/bad.desktop
share/applications/outside.desktop
@ignore
share/applications/ignored.desktop
PLIST
"$tmp/store" browse "$prefix" "$db" >/dev/null
