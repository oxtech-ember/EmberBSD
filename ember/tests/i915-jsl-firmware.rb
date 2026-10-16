# SPDX-License-Identifier: BSD-2-Clause
# Execute the production firmware table/selector; loading remains outside scope.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
intel = root + '/sys/external/bsd/drm2/dist/drm/i915'
source = File.read(intel + '/gt/uc/intel_uc_fw.c')
a = source.index(/^static void\n__uc_fw_auto_select\(/) or abort 'Missing auto selector'
b = source.index(/^}/, a) or abort 'Unclosed auto selector'
selector = source[a..b]
macro = lambda do |name|
  source[/^#define #{name}\(.*?(?<!\\)\n/m] or abort "Missing #{name}"
end
parts = {
  'LICENSES' => source[0...source.index('#include')].scan(%r{/\*.*?\*/|//[^\n]*}m).join("\n"),
  'ENUMS' => File.read(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m] + "\n" +
    File.read(intel + '/gt/uc/intel_uc_fw.h')[/enum intel_uc_fw_type \{.*?^};/m] + "\n" +
    File.read(intel + '/gt/uc/intel_uc_fw.h')[/^#define INTEL_UC_FW_NUM_TYPES[^\n]*\n/],
  'DEFS' => %w[INTEL_UC_FIRMWARE_DEFS __MAKE_UC_FW_PATH MAKE_GUC_FW_PATH MAKE_HUC_FW_PATH UC_FW_BLOB GUC_FW_BLOB HUC_FW_BLOB MAKE_FW_LIST].map { |n| macro.call(n) }.join +
    source[/struct __packed uc_fw_blob \{.*?^};/m] + "\n" +
    source[/struct __packed uc_fw_platform_requirement \{.*?^};/m],
  'DEFAULT' => File.read(intel + '/i915_params.h')[/param\(int, enable_guc, ([^)]*)\)/, 1] || abort('Missing enable_guc default'),
  'SELECTOR' => selector
}
code = File.read(__dir__ + '/i915-jsl-firmware-fixture.c')
parts.each { |n, v| code.sub!("@#{n}@", v) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-jsl-firmware-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/test')
  abort out + err unless status.success?
  puts out
end
