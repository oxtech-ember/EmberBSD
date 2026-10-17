# SPDX-License-Identifier: BSD-2-Clause
# Execute real GT workaround builders and hardware reset with recorded hooks.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
intel = root + '/sys/external/bsd/drm2/dist/drm/i915'
wa = File.read(intel + '/gt/intel_workarounds.c')
reset = File.read(intel + '/gt/intel_reset.c')
drv = File.read(intel + '/i915_drv.h')
regs = File.read(intel + '/i915_reg.h')
extract = lambda do |s, name, type|
  m = /^#{Regexp.escape(name)}\([^;{}]*?\)\n\{|^[^\n]*\b#{Regexp.escape(name)}\([^;{}]*?\)\n\{/m.match(s) or abort "Missing #{name}"
  a = m.begin(0) + m[0].index(name)
  b = s.index(/^}/, a) or abort "Unclosed #{name}"
  type + ' ' + s[a..b] + "\n"
end
functions = extract.call(wa, 'icl_gt_workarounds_init', 'static void') +
  extract.call(wa, 'rcs_engine_wa_init', 'static void') +
  extract.call(reset, 'gen6_hw_domain_reset', 'static int')
# Extract real object-like register constants and their dependencies.
# Host hooks model register access, not the register addresses or WA bit values.
definitions = regs.scan(/^#define\s+(\w+)([ \t]+.*?(?<!\\)\n)/m).to_h
body = functions.gsub(%r{/\*.*?\*/|"(?:\\.|[^"\\])*"}m, '')
needed = body.scan(/\b[A-Z_][A-Z0-9_]*\b/).uniq
selected = {}
until needed.empty?
  name = needed.shift
  next if selected.key?(name) || !definitions.key?(name)
  selected[name] = definitions.fetch(name)
  needed.concat(selected[name].gsub(%r{/\*.*?\*/}m, '').scan(/\b[A-Z_][A-Z0-9_]*\b/))
end
parts = {
  'LICENSES' => [wa, reset].map { |s| s[0...s.index('#include')].scan(%r{/\*.*?\*/|//[^\n]*}m).join("\n") }.join("\n"),
  'ENUMS' => File.read(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m],
  'REGS' => selected.map { |n, v| '#define ' + n + v }.join +
    regs.lines.grep(/^#define\s+L3_(?:GENERAL|HIGH)_PRIO_CREDITS\(/).join,
  'REVISIONS' => drv.scan(/^#define (?:ICL|TGL)_REVID_\w+[^\n]*\n/).join +
    drv.scan(/^#define IS_(?:REVID|ICL_REVID|TGL_REVID)\(.*?(?<!\\)\n/m).join,
  'FUNCTIONS' => functions
}
code = File.read(__dir__ + '/i915-jsl-gt-fixture.c')
parts.each { |n, v| code.sub!("@#{n}@", v) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-jsl-gt-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -fwrapv -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/test')
  abort out + err unless status.success?
  puts out
end
