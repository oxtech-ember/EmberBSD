# Origin: EmberBSD installer branding regression, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
failures = []
check = ->(ok, what) { failures << what unless ok }
Dir.glob("#{root}/distrib/{amd64,i386,evbarm}/**/boot.cfg*").each do |path|
  rendered = File.binread(path).gsub('@@VERSION@@', '11.0').gsub('@@MACHINE@@', 'amd64')
  visible = rendered.lines.grep(/^(banner|menu)=/).map { |line| line.split(':', 2).first }.join
  check.call(!visible.match?(/based on NetBSD/i), "prominent upstream banner: #{path}")
  check.call(!visible.match?(/(?:Welcome to|Install|Boot) (?:the )?NetBSD/), path)
  check.call(rendered.include?('boot netbsd'), "kernel command changed: #{path}") if rendered.include?('Install ')
end
partition_messages = %w[NetBSD_partition_cant_change parttype_only_disklabel nobsdpart multbsdpart]
Dir.glob("#{root}/usr.sbin/sysinst/{msg.*,arch/{amd64,i386}/msg.*}").each do |path|
  File.binread(path).scan(/\bmessage\s+(\w+)\s*\{([^}]*)\}/m) do |id, body|
    next if partition_messages.include?(id)
    # URLs and addresses refer to upstream documentation and authors.
    text = body.gsub(%r{https?://\S+|[\w.@/-]+\.org\S*}, '')
    check.call(!text.include?('NetBSD'), "#{path}: #{id}")
  end
end
Dir.mktmpdir('ember-branding') do |dir|
  _, err, status = Open3.capture3('sh', "#{root}/sys/conf/newvers_stand.sh", '-d', '-m', 'amd64',
    "#{root}/sys/arch/i386/stand/efiboot/version", 'EFI Boot', chdir: dir)
  check.call(status.success?, err)
  version = File.read("#{dir}/vers.c")
  check.call(version.include?('EmberBSD/amd64 EFI Boot'), 'generated bootloader name')
end
defaults = File.read("#{root}/usr.sbin/sysinst/defs.h")
sets = defaults.split('#if !defined(SYSINST_PKG_HOST)').first
check.call(!sets.match?(/(?:ftp|cdn|daily).*NetBSD/), 'OS sets still default to NetBSD')
check.call(sets.include?('cdn.emberbsd.oxteam.me'), 'EmberBSD CDN default missing')
make = File.read("#{root}/usr.sbin/sysinst/Makefile.inc")
check.call(!make.match?(/-DSYSINST_(?:FTP|HTTP)_HOST=.*NetBSD/), 'build overrides own set server')
if failures.empty?
  puts 'PASS: installer branding, preserved kernel commands, generated loader name and own set source'
else
  warn failures.join("\n")
  abort "FAIL: #{failures.length} branding regressions"
end
