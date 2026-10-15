#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source FFS list-format contract.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
abort 'Usage: ffs-xattr-list.rb SOURCE_ROOT FFS_EXTATTR_SOURCE NEW_OUTPUT' unless ARGV.length == 3
root, source, output = ARGV.map { |path| File.expand_path(path) }
Dir.mkdir(output)
body = File.read(source)[/^int\nffs_listextattr\(void \*v\)\n\{.*?^\}/m] or abort 'Missing actual ffs_listextattr function'
File.write(File.join(output, 'ffs-listextattr-body.h'), body + "\n")
FileUtils.cp(File.join(root, 'sys/ufs/ufs/extattr.h'), File.join(output, 'ffs-extattr-layout.h'))
constants = File.readlines(File.join(root, 'sys/sys/extattr.h')).grep(/^#define\s+EXTATTR_(?:NAMESPACE_(?:USER|SYSTEM|EMPTY)|LIST_LENPREFIX)\s/)
abort 'Missing namespace/list constants' unless constants.length == 4
File.write(File.join(output, 'ffs-extattr-constants.h'), constants.join)
File.write(File.join(output, 'inputs.json'), JSON.pretty_generate({
  source: source, source_sha256: Digest::SHA256.file(source).hexdigest,
  fixture_sha256: Digest::SHA256.file(File.join(root, 'ember/tests/ffs-xattr-list.c')).hexdigest,
  layout_sha256: Digest::SHA256.file(File.join(root, 'sys/ufs/ufs/extattr.h')).hexdigest,
  constants_sha256: Digest::SHA256.file(File.join(root, 'sys/sys/extattr.h')).hexdigest
}) + "\n")
commands = []
[false, true].each do |ufs|
  variant = ufs ? 'ufs1' : 'no-ufs1'
  binary = File.join(output, variant)
  command = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer]
  command << '-DUFS_EXTATTR' if ufs
  command += ["-I#{output}", File.join(root, 'ember/tests/ffs-xattr-list.c'), '-o', binary]
  commands << command
  File.write(File.join(output, 'commands.json'), JSON.pretty_generate(commands) + "\n")
  out, err, status = Open3.capture3(*command)
  File.write(File.join(output, "#{variant}-build.log"), out + err)
  abort out + err unless status.success?
  out, err, status = Open3.capture3({ 'ASAN_OPTIONS' => 'halt_on_error=1', 'UBSAN_OPTIONS' => 'halt_on_error=1' }, binary)
  File.write(File.join(output, "#{variant}-run.log"), out + err)
  File.write(File.join(output, 'commands.json'), JSON.pretty_generate(commands) + "\n")
  abort out + err unless status.success?
  puts out
end
