#!/usr/bin/env ruby
# Origin: EmberBSD actual-source native DRM event contract.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'shellwords'
root = File.expand_path('../..', __dir__)
baseline = ARGV.first == '--expect-baseline-failure'
ARGV.shift if baseline
abort 'usage: drm-hotplug-contract.rb [--expect-baseline-failure] [SOURCE_ROOT]' if ARGV.length > 1
source = ARGV[0] || root
text = File.read(source + '/sys/external/bsd/drm2/drm/drm_sysfs.c')
has_sender = text.include?('drm_sysfs_device_change')
abort 'missing production sender' unless baseline || has_sender
abort 'baseline has a native sender' if baseline && has_sender
names = %w[drm_sysfs_hotplug_event drm_sysfs_connector_status_event]
names.unshift('drm_sysfs_device_change') unless baseline
body = names.map do |name|
  function = text[/^(?:static )?void\n#{name}\(.*?^}/m]
  abort 'missing production function ' + name unless function
  function
end.join("\n\n")
Dir.mktmpdir('drm-hotplug-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  path = work + '/drm-hotplug-body.h'
  File.write(path, body + "\n")
  cmd = [*Shellwords.split(ENV.fetch('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
         '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         *Shellwords.split(ENV.fetch('CFLAGS', '')), '-I' + work,
         root + '/ember/tools/drm-hotplug-contract.c', '-o', work + '/contract']
  abort 'production contract compile failed' unless system(*cmd)
  passed = system(work + '/contract')
  abort 'baseline survived' if baseline && passed
  abort 'production contract failed' if !baseline && !passed
  puts 'Baseline rejected: no native event' if baseline
  if ENV['DRM_HOTPLUG_CAPTURE']
    to = ENV['DRM_HOTPLUG_CAPTURE']
    FileUtils.mkdir_p(to)
    FileUtils.cp(path, to + '/drm-hotplug-body.h')
    File.write(to + '/command.txt', Shellwords.join(cmd) + "\n")
  end
  unless baseline || ENV['DRM_HOTPLUG_SKIP_MUTANTS'] == '1'
    [
      ['registration', '!dev->registered', 'false'],
      ['primary-index', '"dri/card%d", dev->primary->index', '"dri/card%d", dev->render->index'],
      ['reject-release', 'if ((*devmon_insert_vec)("device-change", event) != 0)', 'if ((*devmon_insert_vec)("device-change", event) == 0)'],
      ['irq-context', 'cpu_intr_p() || cpu_softintr_p()', 'false']
    ].each do |name, before, after|
      abort 'mutant anchor missing' unless body.include?(before)
      File.write(path, body.sub(before, after))
      abort 'mutant compile failed: ' + name unless system(*cmd)
      survived = File.open(work + '/mutant.log', 'w') do |log|
        system(work + '/contract', out: log, err: log)
      end
      abort 'mutant survived: ' + name if survived
      puts 'Mutant rejected: ' + name
    end
  end
end
