# SPDX-License-Identifier: BSD-2-Clause
# Catch builds from the working tree, mismatched image/kernel IDs and reused output.
require 'open3'
require 'tmpdir'
require 'fileutils'

builder = File.expand_path('../release/build-amd64.sh', __dir__)
Dir.mktmpdir('ember-release-build') do |dir|
  repo = File.join(dir, 'repo')
  FileUtils.mkdir_p(File.join(repo, 'ember/release'))
  FileUtils.cp(builder, File.join(repo, 'ember/release/build-amd64.sh')) if File.file?(builder)
  script = <<~SH
    #!/bin/sh
    printf '%s\n' "$@" > ../build-arguments
    printf 'clean committed source\n' > ../source-proof
    ulimit -s > ../stack-proof
  SH
  File.write(File.join(repo, 'build.sh'), script)
  %w[init add commit].each do |action|
    args = case action
    when 'init' then ['git', 'init', '-q', repo]
    when 'add' then ['git', '-C', repo, 'add', '.']
    else ['git', '-C', repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture']
    end
    _, err, status = Open3.capture3(*args)
    abort err unless status.success?
  end
  sha, err, status = Open3.capture3('git', '-C', repo, 'rev-parse', 'HEAD')
  abort err unless status.success?
  sha = sha.strip
  File.write(File.join(repo, 'build.sh'), "exit 99\n")
  output = File.join(dir, 'output')
  out, err, status = Open3.capture3('sh', File.join(repo, 'ember/release/build-amd64.sh'), sha, output)
  abort "FAIL: pinned build should succeed: #{out}#{err}" unless status.success?
  args = File.readlines(File.join(output, 'build-arguments'), chomp: true)
  stack = File.read(File.join(output, 'stack-proof')).strip
  abort "FAIL: bootstrap compiler stack limit is too small: #{stack} KiB" unless stack == 'unlimited' || stack.to_i >= 65520
  ["EMBER_BUILD_ID=#{sha}", "BUILDINFO=EmberBSD:#{sha}", 'KERNEL_SETS=GENERIC',
   'BUILD_KERNELS=INSTALL', 'MKGCCCMDS=no', 'SYSINST_HTTP_HOST=github.com', 'SYSINST_FTP_SET_DIR_BIN=',
   "SYSINST_FTP_DIR=oxtech-ember/EmberBSD/releases/download/build-#{sha}-amd64-no-x11"].each do |value|
    abort "FAIL: missing pinned build value #{value}" unless args.include?(value)
  end
  abort 'FAIL: incomplete build operations' unless args.last(4) == %w[tools release install-image iso-image]
  _, _, status = Open3.capture3('sh', builder, sha, output)
  abort 'FAIL: existing output was accepted' if status.success?
  _, _, status = Open3.capture3('sh', builder, 'main', File.join(dir, 'bad-pin'))
  abort 'FAIL: mutable revision was accepted' if status.success?
end
puts 'PASS: build uses pinned sources, matching image/kernel IDs, GitHub paths and unused output'
