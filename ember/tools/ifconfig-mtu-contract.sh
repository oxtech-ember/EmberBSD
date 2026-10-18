#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# AI-assisted actual-source MTU setter and matched-command consumer contract.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/ifconfig-mtu.XXXXXXXX")
trap 'rm -rf "$work"' 0 HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby - "$src" "$work" "${IFCONFIG_MTU_SOURCE:-$src/sbin/ifconfig/ifconfig.c}" \
    "${IFCONFIG_MTU_PARSER_SOURCE:-$src/sbin/ifconfig/parse.c}" <<'RUBY'
src,work,cli,parser=ARGV
def notice(s)
    s[0...(s.index(/^#include/) || abort('missing notice boundary'))]
end
def function(s,name,type)
    body=s[/^#{Regexp.escape(name)}\(.*?^\}/m] || abort('missing '+name)
    type+"\n"+body+"\n"
end
c=File.read(cli); p=File.read(parser)
u=File.read(src+'/sbin/ifconfig/util.c'); h=File.read(src+'/sbin/ifconfig/parse.h')
File.write(work+'/mtu-source.inc',notice(c)+notice(p)+notice(u)+
    function(u,'direct_ioctl','int')+function(c,'setifmtu','static int')+
    function(p,'matches_exec','int'))
File.write(work+'/mtu-parser-types.inc',
    h[/^struct match;.*?(?=^struct branch \{)/m] || abort('missing parser types'))
h=File.read(src+'/sys/net/if.h')
request=h.lines.grep(/^#define\s+IF_NAME(?:SIZE|SIZ)\s/).join
request+=h.lines.grep(/^#define\s+IFNAMSIZ\s/).join
request+=h[/^struct\s+ifreq \{.*?^\};/m] || abort('missing actual ifreq')
io=File.read(src+'/sys/sys/sockio.h')
request+="\n"+io.lines.grep(/^#define\s+SIOCSIFMTU\s/).join
File.write(work+'/mtu-request.inc',h[0...(h.index(/^#ifndef/) || abort('missing header guard'))]+
    File.read(src+'/sys/sys/ioccom.h')+"\n"+request)
RUBY
case ${IFCONFIG_MTU_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
"${CC:-cc}" -std=c11 -O2 -g -Wall -Werror -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/ifconfig-mtu-contract.c" -o "$work/test"
if [ "${IFCONFIG_MTU_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" \
        "$src/ember/tools/ifconfig-mtu-contract.c" 2> "$work/cppcheck.log"
    cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -Wall -Werror -D_DEFAULT_SOURCE \
        -D_DARWIN_C_SOURCE -I"$work" "$@" \
        "$src/ember/tools/ifconfig-mtu-contract.c" -o "$work/analysis.plist" \
        2> "$work/csa.log"
    cat "$work/csa.log"; test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
