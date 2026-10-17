# SPDX-License-Identifier: BSD-2-Clause
# Exercise production JSL identity and consumers without enabling attachment.
require 'tmpdir'
require 'open3'
require 'shellwords'

root = File.expand_path(ARGV[0] || '../..', __dir__)
drm = root + '/sys/external/bsd/drm2'
intel = drm + '/dist/drm/i915'
read = ->(path) { File.read(path) }
extract = lambda do |source, name|
  start = source.index(/\b#{Regexp.escape(name)}\([^;]*?\)\n\{/m) or abort "Missing #{name}"
  finish = source.index(/^}/, start) or abort "Unclosed #{name}"
  source[start...finish + 1]
end
info = read.call(intel + '/intel_device_info.c')
pci = read.call(intel + '/i915_pci.c')
drv = read.call(intel + '/i915_drv.h')
pch = read.call(intel + '/intel_pch.c')
stolen = read.call(drm + '/i915drm/intel_gtt_subr.c')
table = pci[/static const struct pci_device_id pciidlist\[\] = \{.*?^};/m] or abort 'Missing PCI table'
infos = table.scan(/&(\w+_info)/).flatten.uniq.map do |name|
  if %w[icl_info ehl_info jsl_info].include?(name)
    pci[/static const struct intel_device_info #{name} = \{.*?^};/m] or abort "Missing #{name}"
  else
    "static const struct intel_device_info #{name} = {0};"
  end
end.join("\n")
stolen_table = stolen[/static const struct pci_device_id intel_stolen_ids\[\] = \{.*?^};/m] or abort 'Missing stolen table'
ops = stolen_table.scan(/&(\w+_ops)/).flatten.uniq.map do |name|
  "static const int #{name} = #{name == 'gen11_stolen_ops' ? 11 : 0};"
end.join("\n")
# Preserve each imported license in the generated compilation unit.
licenses = [info, pci, drv, pch, stolen].map do |s|
  s[0...s.index('#include')].scan(%r{/\*.*?\*/|//[^\n]*}m).join("\n")
end.join("\n")
enum = read.call(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m]
engines = read.call(intel + '/gt/intel_engine_types.h')[/enum intel_engine_id \{.*?^};/m]
predicates = drv.scan(/^#define IS_(?:I\d\w*|G\d\w*|PINEVIEW|IRONLAKE|SANDYBRIDGE|IVYBRIDGE|VALLEYVIEW|HASWELL|BROADWELL|CHERRYVIEW|SKYLAKE|BROXTON|KABYLAKE|GEMINILAKE|COFFEELAKE|CANNONLAKE|ICELAKE|ELKHARTLAKE|JASPERLAKE|JSL_EHL|TIGERLAKE)\(dev_priv\).*?(?<!\\)\n/m).join
pch_header = read.call(intel + '/intel_pch.h')
pch_defs = pch_header[/enum intel_pch \{.*?^};/m] + "\n" + pch_header.lines.grep(/^#define INTEL_PCH_.*DEVICE_ID_(?:TYPE|MASK)/).join
code = File.read(__dir__ + '/i915-jsl-platform-fixture.c')
parts = {
  'LICENSES' => licenses,
  'ENUMS' => enum + "\n" + engines + "\n" + pch_defs,
  'IDS' => read.call(drm + '/dist/include/drm/i915_pciids.h'),
  'PREDICATES' => predicates,
  'MASK' => 'static unsigned int ' + extract.call(drv, '__platform_mask_index') + "\n" +
    'static unsigned int ' + extract.call(drv, '__platform_mask_bit') + "\n" +
    'static bool ' + extract.call(drv, 'IS_PLATFORM'),
  'INFOS' => infos + "\n" + table,
  'STOLEN' => ops + "\n" + stolen_table,
  'NAMES' => info[/#define PLATFORM_NAME\(x\).*?^#undef PLATFORM_NAME/m] + "\nconst char *" +
    extract.call(info, 'intel_platform_name'),
  'SSEU' => 'static void ' + extract.call(info, 'gen11_sseu_info_init'),
  'PCH' => 'static enum intel_pch ' + extract.call(pch, 'intel_pch_type') + "\nstatic unsigned short " +
    extract.call(pch, 'intel_virt_detect_pch'),
  'LOOKUP' => 'static const struct pci_device_id *' + extract.call(
    read.call(drm + '/i915drm/i915_pci_autoconf.c'), 'i915drmkms_pci_lookup')
}
parts.each { |name, text| code.sub!("@#{name}@", text) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-i915-jsl-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  # The production PCI table uses its existing partial zero sentinel.
  out, err, status = Open3.capture3(*cc, *flags, '-Wno-missing-field-initializers',
    work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/test')
  abort out + err unless status.success?
  puts out
end
