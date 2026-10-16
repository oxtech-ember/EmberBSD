# SPDX-License-Identifier: BSD-2-Clause
# Execute production FBC and display-power functions with host state/MMIO hooks.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
intel = root + '/sys/external/bsd/drm2/dist/drm/i915'
power = File.read(intel + '/display/intel_display_power.c')
fbc = File.read(intel + '/display/intel_fbc.c')
extract = lambda do |s, name, type|
  m = /^#{Regexp.escape(name)}\([^;{}]*?\)\n\{|^[^\n]*\b#{Regexp.escape(name)}\([^;{}]*?\)\n\{/m.match(s) or abort "Missing #{name}"
  a = m.begin(0) + m[0].index(name)
  b = s.index(/^}/, a) or abort "Unclosed #{name}"
  type + ' ' + s[a..b] + "\n"
end
drv = File.read(intel + '/i915_drv.h')
regs = File.read(intel + '/i915_reg.h')
functions = extract.call(fbc, 'intel_fbc_can_activate', 'static bool') +
  extract.call(power, 'intel_power_domains_init', 'static int') +
  extract.call(power, 'intel_display_power_suspend_late', 'static void') +
  extract.call(power, 'intel_display_power_resume_early', 'static void')
# Keep inherited selectors real. Models supply only device fields and side effects.
predicates = drv.scan(/^#define IS_(?:I\d\w*|G\d\w*|GM45|PINEVIEW|IRONLAKE|SANDYBRIDGE|IVYBRIDGE|VALLEYVIEW|HASWELL|BROADWELL|CHERRYVIEW|SKYLAKE|BROXTON|KABYLAKE|GEMINILAKE|COFFEELAKE|CANNONLAKE|ICELAKE|ELKHARTLAKE|JASPERLAKE|JSL_EHL|TIGERLAKE|GEN9_BC|GEN9_LP|LP|CNL_WITH_PORT_F)\(dev_priv\).*?(?<!\\)\n/m).join
parts = {
  'LICENSES' => [power, fbc].map { |s| s[0...s.index('#include')].scan(%r{/\*.*?\*/|//[^\n]*}m).join("\n") }.join("\n"),
  'ENUMS' => File.read(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m] + "\n" +
    File.read(intel + '/intel_pch.h')[/enum intel_pch \{.*?^};/m],
  'PREDICATES' => predicates,
  # Before the patch the missing bit is left undefined; baseline has no caller.
  'REGS' => regs.lines.grep(/^#define\s+(?:SOUTH_CHICKEN1|SBCLK_RUN_REFCLK_DIS)\s/).join,
  'TABLES' => functions.scan(/set_power_wells\(power_domains, (\w+)\)/).flatten.uniq.map { |n| "static const int #{n}[] = {0};" }.join("\n"),
  'FUNCTIONS' => functions
}
code = File.read(__dir__ + '/i915-jsl-power-fixture.c')
parts.each { |n, v| code.sub!("@#{n}@", v) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-jsl-power-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/test')
  abort out + err unless status.success?
  puts out
end
