# SPDX-License-Identifier: BSD-2-Clause
# Reuse a recorded kernel compile command. Only source and output may change.
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
require 'fileutils'

log, config, source, work = ARGV
abort 'Usage: ruby usl-sync-target.rb BUILD_LOG KERNEL_CONFIG SOURCE RECEIPT' unless work
source, work, config = [source, work, config].map { |p| File.expand_path(p) }
abort 'Missing configured options' unless File.file?(config + '/opt_wsdisplay_compat.h')
context = nil
commands = File.readlines(log).filter_map.with_index do |line, number|
  if (match = /^(?:===> Build directory:\s*|Build directory is\s+)(.+?)\s*$/.match(line))
    context = File.expand_path(match[1])
  end
  next unless line.include?('/wsdisplay_compat_usl.c') && line.include?(' -c ')
  next unless context == config
  words = Shellwords.split(line)
  next unless words.first.end_with?('gcc') && words.include?('-Werror')
  next unless words.count { |s| s.end_with?('/wsdisplay_compat_usl.c') } == 1
  [words, number + 1]
end
abort 'Missing real kernel compile command for this configured directory' if commands.empty?
argv, line_number = commands.last
FileUtils.mkdir_p(work)
File.write(work + '/original-argv.json', JSON.pretty_generate(argv) + "\n")
argv[argv.index { |s| s.end_with?('/wsdisplay_compat_usl.c') }] = source
abort 'Missing unique output switch' unless argv.count('-o') == 1
argv[argv.index('-o') + 1] = work + '/wsdisplay_compat_usl.o'
File.write(work + '/argv.json', JSON.pretty_generate(argv) + "\n")
File.write(work + '/inputs.json', JSON.pretty_generate({
  log: File.expand_path(log), config: config, source: source,
  log_line: line_number,
  source_sha256: Digest::SHA256.file(source).hexdigest,
  command_sha256: Digest::SHA256.file(work + '/argv.json').hexdigest
}) + "\n")
out, err, status = Open3.capture3(*argv, chdir: config)
File.write(work + '/compile.log', out + err)
abort out + err unless status.success?
puts 'PASS recorded target kernel TU with unchanged flags and configured headers'
