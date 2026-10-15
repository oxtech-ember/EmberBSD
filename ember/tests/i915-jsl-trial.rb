# SPDX-License-Identifier: BSD-2-Clause
# Run the real native PCI guard with explicit option states and narrow identity.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
drm = root + '/sys/external/bsd/drm2'
intel = drm + '/dist/drm/i915'
source = File.read(drm + '/i915drm/i915_pci_autoconf.c')
a = source.index(/^i915drmkms_pci_lookup\([^;]*?\)\n\{/m) or abort 'Missing native lookup'
b = source.index(/^}/, a) or abort 'Unclosed native lookup'
option_include = source[/^#ifdef _KERNEL_OPT\n#include "opt_ember_i915_jsl_trial.h"\n#endif/]
parts = {
  'LICENSE' => source[0...source.index('#include')],
  'OPTION' => option_include || '',
  'ENUMS' => File.read(intel + '/intel_device_info.h')[/enum intel_platform \{.*?^};/m] + "\n" +
    File.read(intel + '/gt/intel_engine_types.h')[/enum intel_engine_id \{.*?^};/m],
  'INFO' => File.read(intel + '/i915_pci.c')[/static const struct intel_device_info jsl_info = \{.*?^};/m],
  'LOOKUP' => 'static const struct pci_device_id *' + source[a..b]
}
code = File.read(__dir__ + '/i915-jsl-trial-fixture.c')
parts.each { |n, v| code.sub!("@#{n}@", v) }
abort 'Unexpanded fixture marker' if code.match?(/@[A-Z]+@/)
Dir.mktmpdir('ember-jsl-trial-') do |work|
  File.write(work + '/test.c', code)
  states = {'GENERIC' => false, 'INSTALL' => false, 'GENERIC_JSL_TRIAL' => true}
  generated = ENV['NETBSD_CONFIG']
  states.each do |name, enabled|
    dir = work + '/' + name
    FileUtils.mkdir_p(dir)
    if generated
      out, err, status = Open3.capture3(*Shellwords.split(generated), '-s', root + '/sys', '-b', dir,
        root + '/sys/arch/amd64/conf/' + name, chdir: work)
      abort out + err unless status.success?
      header = File.read(dir + '/opt_ember_i915_jsl_trial.h')
      actual = header.match?(/^#define\s+EMBER_I915_JSL_4E55_TRIAL\s+1\b/)
      abort "Wrong generated option for #{name}: #{header}" unless actual == enabled
    else
      # These headers model config(1) output; set NETBSD_CONFIG to verify generation.
      File.write(dir + '/opt_ember_i915_jsl_trial.h', enabled ? "#define EMBER_I915_JSL_4E55_TRIAL 1\n" : "/* option disabled */\n")
    end
    cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
    flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
    # The explicit include must connect the generated header to the real guard.
    out, err, status = Open3.capture3(*cc, *flags, '-D_KERNEL_OPT', '-I' + dir,
      '-DEXPECT_TRIAL=' + (enabled ? '1' : '0'), work + '/test.c', '-o', dir + '/test')
    abort out + err unless status.success?
    out, err, status = Open3.capture3(dir + '/test')
    abort out + err unless status.success?
    puts name + ': ' + out.strip
  end
  # Standalone modules have no generated kernel option header and keep the guard closed.
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, '-DEXPECT_TRIAL=0', work + '/test.c', '-o', work + '/module')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(work + '/module')
  abort out + err unless status.success?
  puts 'default module: ' + out.strip
  puts(generated ? 'Real NetBSD config headers: PASS' : 'Config generation not run; set NETBSD_CONFIG for that check')
end
