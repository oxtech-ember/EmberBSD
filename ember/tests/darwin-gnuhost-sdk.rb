#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# GNU sub-makes clear the environment: exercise their actual BUILD_ENV.
require 'fileutils'
require 'open3'
require 'shellwords'
src, make, gcc, sdk, work = ARGV
abort 'usage: darwin-gnuhost-sdk.rb SOURCE MAKE GCC SDK NEW_WORK' unless ARGV.size == 5
abort 'NEW_WORK exists' if File.exist?(work)
FileUtils.mkdir_p(work)
input = File.join(work, 'input.c')
File.write(input, "#include <stdio.h>\nint main(void) { return puts(\"SDK available\") < 0; }\n")
out, err, status = Open3.capture3({'SDKROOT'=>sdk}, make, '-C', File.join(src,'tools/binutils'),
  "NETBSDSRCDIR=#{src}", "HOST_CC=#{gcc}", '-V', '${BUILD_ENV}')
abort "Makefile: #{err}" unless status.success?
File.write(File.join(work,'build-env.log'), out)
# The shell expands the caller PATH before env -i; reproduce that expansion.
args = Shellwords.split(out).map { |arg| arg.gsub('$PATH', ENV.fetch('PATH')) }
# All inherited environment is deliberately removed, matching gnuhost gmake.
out, err, status = Open3.capture3('/usr/bin/env', '-i', *args, gcc, input, '-o', File.join(work,'probe'))
File.write(File.join(work,'compile.log'),out+err)
abort 'FAIL SDK lost through actual BUILD_ENV' unless status.success?
out, err, status = Open3.capture3(File.join(work,'probe'))
abort "FAIL SDK executable: #{out}#{err}" unless status.success? && out == "SDK available\n"
puts 'PASS SDK retained through actual cleared GNU build environment'
