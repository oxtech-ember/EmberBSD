#!/bin/sh
# Origin: EmberBSD portable ASCII boot logo checks, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/ember/tests/boot-logo.c" -o "$tmp/logo-test"
"$tmp/logo-test" > "$tmp/logo.txt"
ruby - "$tmp/logo.txt" "$root" <<'RUBY'
logo = File.binread(ARGV[0])
raise 'logo contains terminal controls or non-ASCII bytes' unless logo.bytes.all? { |b| b == 10 || (32..126).cover?(b) }
rows = logo.lines
raise 'logo is not large enough' unless rows.count { |r| r.include?('#') } >= 7
raise 'logo exceeds an 80-column console' unless rows.all? { |r| r.chomp.bytesize <= 79 }
raise 'logo exceeds its nine-line budget' unless rows.size <= 9
# The BIOS menu prints a blank line, options, a blank line and a prompt.
Dir.glob(File.join(ARGV[1], 'distrib/{amd64,i386,evbarm}/**/boot.cfg*')).each do |path|
  config = File.readlines(path)
  banner = config.count { |l| l.start_with?('banner=') }
  menu = config.count { |l| l.start_with?('menu=') }
  next if menu.zero?
  lines = rows.size + banner + menu + 3
  raise "#{path}: #{lines} rows exceed the 25-line menu budget" if lines > 25
end
puts 'PASS: printable ASCII, large wordmark, 80x25 installation menu budget'
RUBY
cat "$tmp/logo.txt"
