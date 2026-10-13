# Origin: EmberBSD complete amd64 ISO artifact regression, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
image = ARGV.fetch(0) { abort 'Usage: ruby ember/tests/installer-image.rb full-amd64.iso [build-id]' }
build = ARGV[1]
extract = lambda do |path|
  data, error, status = Open3.capture3('tar', '-xOf', image, path)
  abort "#{path}: #{error}" unless status.success?
  data
end
config = extract.call('boot.cfg')
abort 'ISO lost its EmberBSD installation banner' unless config.match?(/^banner=EmberBSD/)
abort 'ISO lost its installation menu' unless config.lines.include?("menu=Install EmberBSD:boot netbsd\n")
abort 'ISO does not clear the mascot screen' unless config.lines.include?("clear=1\n")
abort 'ISO shows a prominent upstream banner' if config.match?(/^banner=.*NetBSD/)
list, error, status = Open3.capture3('tar', '-tf', image)
abort error unless status.success?
%w[base etc kern-GENERIC].each do |set|
  abort "ISO has no #{set} set" unless list.lines.include?("amd64/binary/sets/#{set}.tar.xz\n")
end
binary = extract.call('usr/sbin/sysinst')
abort 'ISO installer has no own set source' unless binary.include?('cdn.emberbsd.oxteam.me')
if build
  %w[de es fr pl].each do |lang|
    data = extract.call("usr/share/sysinst/catalog/sysinstmsgs.#{lang}")
    abort "#{lang}: catalog header" unless data.start_with?("MSGTXTS\0")
    abort "#{lang}: wrong image build ID" unless data.include?("Build: #{build}")
  end
end
puts 'PASS: actual ISO installation banner, preserved boot command, own sets/server and requested catalog build ID'
