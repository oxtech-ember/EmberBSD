# SPDX-License-Identifier: BSD-2-Clause
require_relative 'github'

begin
  action = ARGV.shift
  dir = ARGV.shift
  raise EmberRelease::Error, 'Missing build or kit directory' unless dir
  case action
  when 'prepare'
    output = ARGV.shift or raise EmberRelease::Error, 'Use a new output directory'
    receipt = ARGV.shift
    raise EmberRelease::Error, 'Too many arguments' unless ARGV.empty?
    m = EmberRelease.prepare(File.expand_path(dir), File.expand_path(output), receipt)
    puts "Prepared #{m['release_tag']}; VM qualification: #{m['qualification'] ? 'accepted' : 'pending'}"
  when 'verify'
    raise EmberRelease::Error, 'Too many arguments' unless ARGV.empty?
    m = EmberRelease.verify(File.expand_path(dir))
    puts "PASS: #{m['release_tag']} local composition and checksums"
  when 'draft', 'publish', 'verify-remote'
    raise EmberRelease::Error, 'Too many arguments' unless ARGV.empty?
    publisher = EmberRelease::GitHub.new(dir)
    result = action == 'verify-remote' ? publisher.verify_remote['html_url'] : publisher.public_send(action)
    puts result
  else
    raise EmberRelease::Error, 'Use prepare BUILD NEW-KIT [VM-RECEIPT], verify KIT, draft KIT, publish KIT, or verify-remote KIT'
  end
rescue EmberRelease::Error, SystemCallError => e
  warn "Release stopped: #{e.message}"
  exit 1
end
