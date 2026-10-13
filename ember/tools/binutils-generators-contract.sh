#!/bin/sh
# Origin: EmberBSD; AI-assisted regression for complete BFD generator recipes.
# SPDX-License-Identifier: BSD-2-Clause
# Usage: GNU_MAKE=/path/to/gmake BSD_MAKE=/path/to/nbmake sh "$0" \
#   BASE_BFD_SOURCE FIXED_BFD_SOURCE NEW_ABSOLUTE_WORK
# Executes the 18 original recipes from each Makefile, not a replacement
# generator. This checks source generation only, not a complete binutils build.
set -eu
[ "$#" -eq 3 ] || { echo "Usage: $0 BASE_BFD_SOURCE FIXED_BFD_SOURCE NEW_ABSOLUTE_WORK" >&2; exit 2; }
: "${GNU_MAKE:=gmake}" "${BSD_MAKE:=bmake}"
GNU_MAKE=$(command -v "$GNU_MAKE")
BSD_MAKE=$(command -v "$BSD_MAKE")
SED=$(command -v sed)
export GNU_MAKE BSD_MAKE SED
exec ruby - "$0" "$@" <<'RUBY'
require 'digest'
require 'fileutils'
require 'open3'
require 'pathname'

script, base, fixed, work = ARGV
abort 'Absolute source/work paths required' unless [base, fixed, work].all? { |p| Pathname.new(p).absolute? }
abort 'Work directory already exists' if File.exist?(work)
tools = { 'gnu' => ENV.fetch('GNU_MAKE'), 'bsd' => ENV.fetch('BSD_MAKE') }
abort 'Tools must resolve to absolute paths' unless (tools.values + [ENV.fetch('SED')]).all? { |p| Pathname.new(p).absolute? }
pins = {
  'Makefile.am' => 'ffd64f09c005bcd80ba739244201ff579ae977d4f26e823b9fb7680022377f92',
  'Makefile.in' => 'b2200ddc7a025a9b0922c3ea86d2f56b19f8cda3b3ba5b912db0ff5bf329729b'
}
pins.each do |name, sum|
  abort "Unexpected baseline #{name}" unless Digest::SHA256.file(File.join(base, name)).hexdigest == sum
end
FileUtils.mkdir_p(work)
gnu_version, status = Open3.capture2e(tools['gnu'], '--version')
abort 'GNU make required' unless status.success? && gnu_version.start_with?('GNU Make ')
bsd_identity, status = Open3.capture2e(tools['bsd'], '-f', '/dev/null', '-V', '.MAKE')
abort 'BSD make required' unless status.success? && !bsd_identity.strip.empty?
File.write(File.join(work, 'make-tools.txt'), "#{gnu_version}\nBSD .MAKE: #{bsd_identity}")

# Independent output oracle: target, source template, substitution, line marker.
cases = []
%w[32 64].each { |bits| cases << ["elf#{bits}-target.h", 'elfxx-target.h', 'NN', bits, false] }
%w[aarch64 ia64 kvx loongarch riscv].each do |arch|
  %w[32 64].each { |bits| cases << ["elf#{bits}-#{arch}.c", "elfnn-#{arch}.c", 'NN', bits, true] }
end
{
  'peigen.c' => 'pe', 'pepigen.c' => 'pep', 'pex64igen.c' => 'pex64',
  'pe-aarch64igen.c' => 'peAArch64', 'pe-loongarch64igen.c' => 'peLoongArch64',
  'pe-riscv64igen.c' => 'peRiscV64'
}.each { |target, replacement| cases << [target, 'peXXigen.c', 'XX', replacement, true] }
raise 'Expected 18 generators' unless cases.length == 18
inputs = cases.map { |c| c[1] }.uniq
inputs.each do |name|
  abort "Template changed outside this fix: #{name}" unless File.binread(File.join(base, name)) == File.binread(File.join(fixed, name))
end
receipt_inputs = [File.expand_path(script), *tools.values, ENV.fetch('SED')]
[base, fixed].each { |root| (pins.keys + inputs).each { |name| receipt_inputs << File.join(root, name) } }
File.open(File.join(work, 'inputs.sha256'), 'w') do |out|
  receipt_inputs.each { |path| out.puts "#{Digest::SHA256.file(path).hexdigest}  #{path}" }
end

def extract_rule(text, target, source)
  rules = text.scan(/^#{Regexp.escape(target)}[ \t]*:[ \t]+#{Regexp.escape(source)}[ \t]*\n(?:\t[^\n]*\n)+/)
  abort "Ambiguous or missing complete recipe: #{target}" unless rules.length == 1
  rules.first
end

results = []
%w[baseline fixed].each do |variant|
  root = variant == 'baseline' ? base : fixed
  pins.each_key do |makefile|
    text = File.binread(File.join(root, makefile))
    rules = cases.map { |target, source, *_| extract_rule(text, target, source) }
    selected = File.join(work, variant, makefile)
    FileUtils.mkdir_p(selected)
    # Keep the original complete file, including upstream licensing and notices.
    FileUtils.cp(File.join(root, makefile), File.join(selected, 'original.make'))
    File.write(File.join(selected, 'recipes.make'), rules.join("\n"))
    tools.each do |family, make|
      %w[in-source relative absolute].each do |layout|
        dir = File.join(selected, family, layout)
        FileUtils.mkdir_p(dir)
        source_dir = layout == 'in-source' ? dir : File.join(dir, 'inputs')
        FileUtils.mkdir_p(source_dir)
        inputs.each { |name| FileUtils.cp(File.join(root, name), File.join(source_dir, name)) }
        srcdir = case layout
                 when 'in-source' then '.'
                 when 'relative' then 'inputs'
                 else source_dir
                 end
        lookup = family == 'gnu' ? 'VPATH = $(srcdir)' : '.PATH: ${srcdir}'
        preamble = "srcdir = #{srcdir}\nSED = #{ENV.fetch('SED')}\nAM_V_GEN =\nAM_V_at =\n#{lookup}\n"
        File.write(File.join(dir, 'contract.make'), preamble + rules.join("\n"))
        cases.each do |target, source, token, replacement, line|
          output, status = Open3.capture2e(make, '-r', '-f', 'contract.make', target, chdir: dir)
          File.write(File.join(dir, "#{target}.log"), output)
          expected = (line ? "#line 1 \"#{source}\"\n" : '') + File.binread(File.join(root, source)).gsub(token, replacement)
          target_path = File.join(dir, target)
          exact = status.success? && File.file?(target_path) && File.binread(target_path) == expected
          if variant == 'baseline' && family == 'gnu'
            # The existing BSD-only automatic variable expands to nothing.
            abort "Baseline did not reproduce empty input: #{target}\n#{output}" unless !status.success? && output.match?(/<\s*>>? /)
          elsif variant == 'baseline' && makefile == 'Makefile.am' && target.include?('-kvx.')
            # A second existing typo consumed $e in these two line markers.
            wrong_line = expected.sub('"elfnn-kvx.c"', '"lfnn-kvx.c"')
            abort "Unexpected baseline KVX marker: #{target}" unless status.success? && File.binread(target_path) == wrong_line
          else
            abort "Generator mismatch: #{variant}/#{makefile}/#{family}/#{layout}/#{target}\n#{output}" unless exact
          end
          results << [variant, makefile, family, layout, target, status.exitstatus, exact]
        end
      end
    end
  end
end
File.open(File.join(work, 'results.tsv'), 'w') do |out|
  out.puts "variant\tmakefile\tmake\tlayout\ttarget\tstatus\texact_output"
  results.each { |row| out.puts row.join("\t") }
end
%w[baseline fixed].each do |variant|
  rows = results.select { |row| row[0] == variant }
  puts "#{variant}: #{rows.length} recipe runs, #{rows.count { |row| row[6] }} exact outputs"
end
puts 'PASS: all 18 actual BFD recipes from Makefile.am and Makefile.in with GNU make and bmake, in-source/relative/absolute srcdir.'
puts 'The baseline reproduces GNU make empty-input failures and two Makefile.am KVX line-marker defects. Full toolchain build remains separate.'
RUBY
