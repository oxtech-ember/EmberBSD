#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require_relative 'sysroot-kit'
begin
  action, dir = ARGV.shift(2)
  raise EmberRelease::Error, 'Missing build or kit directory' unless dir
  dir = File.expand_path(dir)
  case action
  when 'prepare'
    output, qualification = ARGV.shift(2)
    raise EmberRelease::Error, 'Use prepare BUILD NEW-KIT [QUALIFICATION]' unless output && ARGV.empty?
    manifest = EmberSysroot.prepare(dir, File.expand_path(output), qualification)
    puts "Prepared #{manifest['release_tag']}; qualification: #{manifest['qualification'] ? 'accepted' : 'pending'}"
  when 'verify'
    raise EmberRelease::Error, 'Too many arguments' unless ARGV.empty?
    puts "PASS: #{EmberSysroot.verify(dir)['release_tag']} composition, source and hashes"
  when 'draft', 'publish', 'verify-remote'
    raise EmberRelease::Error, 'Too many arguments' unless ARGV.empty?
    publisher = EmberSysroot::GitHub.new(dir)
    puts(action == 'verify-remote' ? publisher.verify_remote.fetch('html_url') : publisher.public_send(action))
  else
    raise EmberRelease::Error, 'Use prepare, verify, draft, publish or verify-remote'
  end
rescue EmberRelease::Error, SystemCallError => e
  warn "Sysroot release stopped: #{e.message}"
  exit 1
end
