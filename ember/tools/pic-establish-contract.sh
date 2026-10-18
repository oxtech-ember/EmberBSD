#!/bin/sh
# Origin: EmberBSD (AI-assisted). Exercise actual ARM PIC establish bodies.
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 PIC_C NEW_WORK" >&2; exit 2; }
source=$1
work=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
	SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir "$work"
ruby - "$source" "$work/pic-functions.h" <<'RUBY'
source, target = ARGV
lines = File.readlines(source)
names = %w[pic_unblock_percpu pic_establish_intr_common pic_establish_intr
    pic_disestablish_source intr_establish_xname_common intr_establish_xname
    intr_establish_xname_masked intr_establish intr_disestablish intr_mask
    intr_unmask]
File.open(target, 'w') do |out|
  names.each do |name|
    first = lines.index { |line| line.start_with?(name + '(') }
    if first.nil?
      abort "missing #{name}" unless name.end_with?('_common', '_masked')
      next
    end
    last = ((first + 1)...lines.length).find { |i| lines[i] == "}\n" }
    abort "unterminated #{name}" unless last
    out.write(lines[(first - 1)..last].join)
    out.puts
    out.puts '#define PIC_MASKED_API 1' if name == 'intr_establish_xname_masked'
  end
end
RUBY
for mode in up mp; do
	case "$mode" in up) mp=-UMULTIPROCESSOR;; mp) mp=-DMULTIPROCESSOR;; esac
	${CC:-cc} ${CFLAGS:--O2 -g} -std=gnu11 -Wall -Wextra \
	    "$mp" -I "$work" "$here/pic-establish-contract.c" \
	    -o "$work/check-$mode" > "$work/gcc-$mode.log" 2>&1 || {
		cat "$work/gcc-$mode.log" >&2; exit 1;
	}
	cat "$work/gcc-$mode.log"
	ruby - "$work/gcc-$mode.log" "$work/pic-functions.h" <<'RUBY'
log, source = ARGV
lines = File.readlines(source)
File.foreach(log) do |line|
  next unless line.include?('warning:')
  location = line.match(/\A(.+):(\d+):\d+: warning:/)
  next if location && location[1] == source &&
    line.include?('[-Wsign-compare]') &&
    lines[location[2].to_i - 1]&.strip ==
      'for (off = pic_ipl_offset[ipl]; off < pic_ipl_offset[ipl + 1]; off++) {'
  abort line
end
RUBY
	if [ -n "${TEST_RUNNER:-}" ]; then
		"$TEST_RUNNER" "$work/check-$mode"
	else
		"$work/check-$mode"
	fi
done
if [ "${PIC_ESTABLISH_ANALYZE:-0}" = 1 ]; then
	cppcheck --enable=all --check-level=exhaustive --std=c11 \
	    -DMULTIPROCESSOR -I "$work" --xml --xml-version=2 \
	    "$here/pic-establish-contract.c" > "$work/cppcheck.log" \
	    2> "$work/cppcheck.xml" || { cat "$work/cppcheck.xml" >&2; exit 1; }
	cat "$work/cppcheck.log" "$work/cppcheck.xml"
	# Exact inherited/native-signature findings, with source-line matches.
	# Keep every category in the raw XML; reject unmatched findings.
	ruby - "$work/cppcheck.xml" <<'RUBY'
require 'rexml/document'
doc = REXML::Document.new(File.read(ARGV.fetch(0)))
abort 'missing Cppcheck result' unless doc.elements['results/cppcheck']
known = [
  ['arrayIndexOutOfBoundsCond',
   "Either the condition 'ipl<8' is redundant or the array " \
   "'pic_ipl_offset[9]' is accessed at index 9, which is out of bounds.",
   'off = pic_ipl_offset[ipl + 1] - 1;'],
  ['constVariablePointer', "Variable 'is' can be declared as pointer to const",
   'struct intrsource *is = arg2;'],
  ['staticFunction', "The function 'pic_disestablish_source' should have " \
   'static linkage since it is not used outside of its translation unit.',
   'pic_disestablish_source(struct intrsource *is)']
]
# These model callbacks implement fixed native signatures, not const APIs.
%w[handler block configure].each do |name|
  parameter = name == 'handler' ? 'arg' : 'pic'
  declaration = {
    'handler' => 'handler(void *arg)',
    'block' => 'block(struct pic_softc *pic, size_t base, uint32_t mask)',
    'configure' => 'configure(struct pic_softc *pic, struct intrsource *is)'
  }.fetch(name)
  known << ['constParameterCallback', "Parameter '#{parameter}' can be " \
    "declared as pointer to const. However it seems that '#{name}' is a " \
    "callback function, if '#{parameter}' is declared with const you might " \
    'also need to cast function pointer(s).', declaration]
end
unmatched = []
doc.elements.each('results/errors/error') do |e|
  id, message = e.attributes['id'], e.attributes['msg']
  next if id == 'checkersReport' && e.attributes['severity'] == 'information'
  if id == 'missingIncludeSystem' && e.attributes['severity'] == 'information'
    next if %w[assert stdbool stddef stdint stdio stdlib string].any? do |name|
      message == "Include file: <#{name}.h> not found. Please note: Standard " \
        'library headers do not need to be provided to get proper results.'
    end
  end
  location = e.elements['location']
  line = location && File.readlines(location.attributes['file'])[
    location.attributes['line'].to_i - 1]&.strip
  next if known.any? { |k| k == [id, message, line] }
  unmatched << e.to_s
end
abort unmatched.join("\n") unless unmatched.empty?
RUBY
	${CLANG:-clang} --analyze -std=gnu11 -DMULTIPROCESSOR -I "$work" \
	    -Xanalyzer -analyzer-output=text "$here/pic-establish-contract.c" \
	    > "$work/csa.log" 2>&1 || { cat "$work/csa.log" >&2; exit 1; }
	cat "$work/csa.log"
	[ ! -s "$work/csa.log" ]
fi
