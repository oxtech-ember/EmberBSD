#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted actual-source regression.
require 'fileutils'
require 'open3'
require 'shellwords'

abort 'Usage: pkg-add-dryrun-contract.rb PERFORM_C OUTPUT_DIRECTORY' unless ARGV.length == 2
source = File.expand_path(ARGV[0])
output = File.expand_path(ARGV[1])
abort 'Output must not exist' if File.exist?(output)
FileUtils.mkdir_p(output)
text = File.read(source)
license = text[/\/\*-\n.*?\*\//m] or abort 'Missing source license'
functions = %w[preserve_meta_data_file start_replacing].map do |name|
  match = /^static int\n#{name}\([^\n]*\)\n\{\n.*?^\}/m.match(text) or abort "Missing actual function #{name}"
  line = text[0...match.begin(0)].count("\n") + 1
  "#line #{line} #{source.dump}\n#{match[0]}\n"
end
File.write("#{output}/functions.inc", "#{license}\n" + functions.join("\n"))
FileUtils.cp(File.join(__dir__, 'pkg-add-dryrun-contract.c'), "#{output}/fixture.c")
puts "Extracted both actual functions from #{source} into #{output}"
unless ENV['EXTRACT_ONLY'] == '1'
  compiler = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-std=c99 -O1 -g -Wall -Wextra -Werror'))
  command = [*compiler, *flags, "#{output}/fixture.c", '-o', "#{output}/fixture.host"]
  out, status = Open3.capture2e(*command)
  warn out unless out.empty?
  abort "Host fixture compilation failed: #{command.inspect}" unless status.success?
  cases = "#{output}/cases"
  FileUtils.mkdir(cases)
  ok = system("#{output}/fixture.host", cases)
  exit(ok ? 0 : 1)
end
