#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted actual-source cross regression builder.
set -eu
[ "$#" = 1 ] || { echo "Usage: CROSS_CC=... SYSROOT=... CROSS_AR=... MSGFMT=... $0 NEW_WORK" >&2; exit 2; }
: "${CROSS_CC:?Set the EmberBSD cross C compiler}"
: "${SYSROOT:?Set the verified EmberBSD sysroot}"
: "${CROSS_AR:?Set the matching cross archiver}"
: "${MSGFMT:?Set a native GNU msgfmt}"
case "$SYSROOT:$1" in /*:/*) ;; *) echo "Absolute sysroot/work paths required" >&2; exit 2;; esac
source=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir "$1"
work=$(CDPATH= cd -- "$1" && pwd)
mkdir "$work/lib" "$work/obj" "$work/locale" "$work/locale/cy" "$work/locale/cy/LC_MESSAGES"
for file in gettext textdomain gettext_iconv gettext_dummy strhash sysdep plural_parser; do
    "$CROSS_CC" --sysroot="$SYSROOT" -O2 -g -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wall -Wextra \
        -I"$source/lib/libintl" -I"$source/lib/libc" -c "$source/lib/libintl/$file.c" -o "$work/obj/$file.o"
done
"$CROSS_AR" crs "$work/lib/libintl.a" "$work"/obj/*.o
"$CROSS_CC" --sysroot="$SYSROOT" -shared -Wl,-soname,libintl.so.1 \
    -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now "$work"/obj/*.o -o "$work/lib/libintl.so.1"
"$CROSS_CC" --sysroot="$SYSROOT" -O2 -g -Wall -Wextra -Werror \
    "$source/ember/tests/libintl-singular.c" -L"$work/lib" -l:libintl.so.1 -o "$work/check"
"$CROSS_CC" --sysroot="$SYSROOT" -O2 -g -Wall -Wextra -Werror \
    "$source/ember/tests/libintl-singular.c" "$work/lib/libintl.a" -o "$work/check-static"
"$MSGFMT" -o "$work/locale/cy/LC_MESSAGES/singular-hash.mo" "$source/ember/tests/libintl-singular.po"
"$MSGFMT" --no-hash -o "$work/locale/cy/LC_MESSAGES/singular-search.mo" "$source/ember/tests/libintl-singular.po"
cat > "$work/run.sh" <<'RUN'
#!/bin/sh
set -eu
cd "$(dirname "$0")"
case ${1:-fixed} in
    system) unset LD_LIBRARY_PATH; executables=check ;;
    fixed) LD_LIBRARY_PATH="$PWD/lib"; export LD_LIBRARY_PATH; executables="check check-static" ;;
    *) exit 2 ;;
esac
for executable in $executables; do
    for domain in singular-hash singular-search; do
        echo "$executable"
        "./$executable" "$PWD/locale" "$domain"
    done
done
RUN
(cd "$source" && git rev-parse HEAD && git diff -- lib/libintl/gettext.c) > "$work/source.txt"
shasum -a 256 "$source"/lib/libintl/*.[ch] "$source"/lib/libc/citrus/citrus_namespace.h "$source"/lib/libc/citrus/citrus_region.h "$source"/lib/libc/citrus/citrus_memstream.h "$source"/lib/libc/citrus/citrus_bcs.h "$source"/ember/tests/libintl-singular.* \
    "$work/lib/libintl.so.1" "$work/lib/libintl.a" "$work/check" "$work/check-static" "$work"/locale/cy/LC_MESSAGES/*.mo > "$work/receipt.sha256"
{ "$CROSS_CC" --version; "$CROSS_CC" -dumpmachine; "$MSGFMT" --version; } > "$work/tools.txt"
shasum -a 256 "$SYSROOT/usr/lib/libc.so.12" "$SYSROOT/usr/include/libintl.h" > "$work/sysroot-inputs.sha256"
echo "Built actual-source libintl regression; copy the work directory to EmberBSD and run sh run.sh."
