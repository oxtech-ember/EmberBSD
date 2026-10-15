# SPDX-License-Identifier: BSD-2-Clause
# Run production display selectors with host models for device/MMIO state.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
intel = root + '/sys/external/bsd/drm2/dist/drm/i915'
sources = %w[intel_ddi intel_dp intel_psr intel_combo_phy intel_display intel_dpll_mgr intel_cdclk].to_h do |name|
  [name, File.read(intel + '/display/' + name + '.c')]
end
extract = lambda do |source, name, type, optional = false|
  match = /^(?:[A-Za-z_][\w \t*]*[ \t*])?#{name}\([^;{}]*?\)\n\{/m.match(source)
  next '' if !match && optional
  abort "Missing #{name}" unless match
  start = match.begin(0) + match[0].index(name)
  finish = source.index(/^}/, start) or abort "Unclosed #{name}"
  type + ' ' + source[start..finish] + "\n"
end
drv = File.read(intel + '/i915_drv.h')
display = File.read(intel + '/display/intel_display.h')
regs = File.read(intel + '/i915_reg.h')
predicates = drv.scan(/^#define IS_(?:I\d\w*|G\d\w*|PINEVIEW|IRONLAKE|SANDYBRIDGE|IVYBRIDGE|VALLEYVIEW|HASWELL|BROADWELL|CHERRYVIEW|SKYLAKE|BROXTON|KABYLAKE|GEMINILAKE|COFFEELAKE|CANNONLAKE|ICELAKE|ELKHARTLAKE|JASPERLAKE|JSL_EHL|TIGERLAKE)\(dev_priv\).*?(?<!\\)\n/m).join
ddi = sources.fetch('intel_ddi')
ddis = ddi[/struct cnl_ddi_buf_trans \{.*?^};/m] + "\n" +
  ddi.scan(/static const struct cnl_ddi_buf_trans (?:icl|jsl)_combo_phy_ddi_translations_\w+\[\] = \{.*?^};/m).join("\n")
ddis += extract.call(ddi, 'jsl_get_combo_buf_trans', 'static const struct cnl_ddi_buf_trans *', true)
ddis += extract.call(ddi, 'icl_get_combo_buf_trans', 'static const struct cnl_ddi_buf_trans *')
dp = sources.fetch('intel_dp')
rates = extract.call(dp, 'intel_dp_rate_limit_len', 'static int') +
  extract.call(dp, 'cnl_max_source_rate', 'static int') +
  extract.call(dp, 'icl_max_source_rate', 'static int') +
  extract.call(dp, 'is_low_voltage_sku', 'static bool', true) +
  extract.call(dp, 'ehl_max_source_rate', 'static int', true) +
  extract.call(dp, 'intel_dp_set_source_rates', 'static void')
parts = {
  'LICENSES' => sources.values.map { |s| s[0...s.index('#include')].scan(%r{/\*.*?\*/|//[^\n]*}m).join("\n") }.join("\n"),
  'ENUMS' => File.read(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m] + "\n" +
    display[/enum phy \{.*?^};/m] + "\n" + display[/enum port \{.*?^};/m],
  'PREDICATES' => predicates,
  'VOLTAGE' => regs.lines.grep(/^#define\s+VOLTAGE_INFO_(?:0_85V|0_95V|1_05V|MASK)\s/).join,
  'PHY' => extract.call(sources.fetch('intel_display'), 'intel_phy_is_combo', 'static bool') +
    extract.call(sources.fetch('intel_display'), 'intel_phy_is_tc', 'static bool') +
    extract.call(sources.fetch('intel_display'), 'intel_port_to_phy', 'static enum phy') +
    extract.call(sources.fetch('intel_combo_phy'), 'icl_combo_phy_enabled', 'static bool'),
  'DDI' => ddis, 'RATES' => rates,
  'PSR' => extract.call(sources.fetch('intel_psr'), 'intel_psr2_config_valid', 'static bool'),
  'CLOCK' => extract.call(sources.fetch('intel_cdclk'), 'intel_compute_max_dotclk', 'static int') +
    extract.call(sources.fetch('intel_cdclk'), 'intel_update_max_cdclk', 'static void'),
  'PLL' => extract.call(sources.fetch('intel_dpll_mgr'), 'intel_shared_dpll_init', 'static void')
}
code = File.read(__dir__ + '/i915-jsl-display-fixture.c')
parts.each { |name, text| code.sub!("@#{name}@", text) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-jsl-display-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/test')
  abort out + err unless status.success?
  puts out
end
