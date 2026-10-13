#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted regression of the actual readelf consumer.
set -eu
[ "$#" = 4 ] || { echo 'usage: readelf-split-strings.sh GCC CLANG READELF NEW_WORK' >&2; exit 2; }
gcc=$1 clang=$2 reader=$3 work=$4
for tool in "$gcc" "$clang" "$reader"; do [ -x "$tool" ] || exit 2; done
[ ! -e "$work" ] || { echo 'NEW_WORK already exists.' >&2; exit 2; }
mkdir "$work"
printf 'struct split_record { long value; }; struct split_record split_data;\n' > "$work/input.c"
passed=0 failed=0
check() {
    name=$1 input=$2
    if "$reader" --debug-dump=info "$input" > "$work/$name.info" 2> "$work/$name.err" &&
        [ ! -s "$work/$name.err" ] && ruby - "$work/$name.info" "$name" <<'RUBY'
expected = {'structure_type'=>'split_record', 'member'=>'value',
            'variable'=>'split_data',
            'base_type'=>ARGV[1].start_with?('gcc-') ? 'long int' : 'long'}
seen = {}
producer = false
tag = nil
File.foreach(ARGV[0]) do |line|
  if (m = line.match(/Abbrev Number: \d+ \(DW_TAG_(\w+)\)/))
    tag = m[1]
  elsif line.match?(/Abbrev Number: 0/)
    tag = nil
  elsif line.include?('DW_AT_name') && tag && expected.key?(tag)
    seen[tag] = true if line.match?(/:\s*#{Regexp.escape(expected[tag])}\s*$/)
  elsif line.include?('DW_AT_producer')
    family = ARGV[1].start_with?('gcc-') ? /: GNU C/ : /: (?:Homebrew )?clang version/
    producer = true if line.match?(family)
  end
end
exit(expected.keys.all? { |key| seen[key] } && producer ? 0 : 1)
RUBY
    then
        passed=$((passed + 1)); echo "PASS $name"
    else
        failed=$((failed + 1)); echo "FAIL $name"
    fi
}
for compiler in gcc clang; do
    for version in 4 5; do
        for width in 32 64; do
            for kind in plain split; do
                name=$compiler-v$version-w$width-$kind
                if [ "$compiler" = gcc ]; then set -- "$gcc"; else set -- "$clang" --target=aarch64--netbsd; fi
                if [ "$kind" = split ]; then set -- "$@" -gsplit-dwarf; fi
                "$@" -O2 -gdwarf-$version -gdwarf$width -c "$work/input.c" -o "$work/$name.o"
                check "$name" "$work/$name.o"
                if [ "$kind" = split ]; then check "$name-dwo" "$work/$name.dwo"; fi
            done
        done
    done
done
echo "readelf split strings: $passed passed, $failed failed (24 cases; real ELF consumers)"
[ "$passed" = 24 ] && [ "$failed" = 0 ]
