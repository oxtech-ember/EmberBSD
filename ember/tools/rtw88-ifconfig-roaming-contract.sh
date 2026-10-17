#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Execute native ifconfig keyword parsing and its roaming ioctl consumer.
set -eu
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ifconfig-roaming-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby - "$src" "$work" "${IFCONFIG_ROAMING_SOURCE:-$src/sbin/ifconfig/ieee80211.c}" <<'RUBY'
src, work, cli_source = ARGV
cli = File.read(cli_source)
parser = File.read(src + '/sbin/ifconfig/parse.c')
header = File.read(src + '/sbin/ifconfig/parse.h')
ioctl = File.read(src + '/sys/net80211/ieee80211_ioctl.h')
def notice(s)
  s[/\A.*?^ \*\/\n/m] || abort('Missing source notice')
end
def function(s, name, type)
  body = s[/^#{Regexp.escape(name)}\(.*?^\}/m] or abort('Missing function: ' + name)
  type + "\n" + body + "\n"
end
File.write(work + '/roaming-parser-types.inc',
  header[/^struct match;.*?(?=^struct paddr_prefix \{)/m] || abort('Missing parser types'))
File.write(work + '/roaming-parser.inc', notice(parser) +
  %w[match_setenv pkw_match pkw_setwords].map { |n| function(parser, n, 'static int') }.join +
  function(parser, 'matches_exec', 'int'))
util = File.read(src + '/sbin/ifconfig/util.c')
body = notice(cli) + notice(util) + function(util, 'direct_ioctl', 'static int') +
  function(cli, 'set80211', 'static int')
# Old source deliberately has no roaming command: its parser must reject it.
if cli.match?(/^setifroaming\(/)
  body += function(cli, 'setifroaming', 'static int')
  body += cli[/^static const struct kwinst roamingkw\[\] = \{.*?^\};/m] + "\n"
  body += cli[/^static struct pkw roaming = .*?;\n/m]
end
body += cli[/^static const struct kwinst kw80211kw\[\] = \{.*?^\};/m] + "\n"
body += cli[/^struct pkw kw80211 = .*?;\n/m]
File.write(work + '/roaming-cli.inc', body)
request = notice(ioctl) + (ioctl[/^struct ieee80211req \{.*?^\};/m] ||
  abort('Missing native request'))
netbsd = ioctl[/^#ifdef __NetBSD__\n(.*?)^#endif/m, 1] or abort('Missing native ioctl constants')
request += "\n" + netbsd.lines.grep(/^#define\s+SIOCS80211\s/).join
request += ioctl.lines.grep(/^#define\s+IEEE80211_IOC_ROAMING\s/).join
request += File.read(src + '/sys/net80211/ieee80211.h').lines.grep(
  /^#define\s+IEEE80211_(FRAG_MAX|RTS_MAX)\s/).join
File.write(work + '/roaming-request.inc', request)
Dir.mkdir(work + '/net80211')
File.write(work + '/net80211/_ieee80211.h', File.read(src + '/sys/net80211/_ieee80211.h'))
File.write(work + '/native-ioccom.h', File.read(src + '/sys/sys/ioccom.h'))
queue = File.read(src + '/sys/sys/queue.h')
File.write(work + '/native-simpleq.h', notice(queue) +
  (queue[/^#define\s+SIMPLEQ_HEAD\(.*?(?=^\/\*\n \* Tail queue definitions\.)/m] ||
  abort('Missing native SIMPLEQ definitions')))
RUBY
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE '-D__arraycount(a)=(sizeof(a)/sizeof((a)[0]))' \
    -I"$work" "$tools/rtw88-ifconfig-roaming-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
