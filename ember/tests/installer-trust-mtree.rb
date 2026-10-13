# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
require 'fileutils'
root = File.expand_path('../..', __dir__)
tool = ARGV.fetch(0)
specs = %w[distrib/amd64/cdroms/spec.in distrib/amd64/cdroms/spec-dir.in distrib/amd64/installimage/spec.inst]
Dir.mktmpdir('ember-trust-mtree') do |dir|
  tree = File.join(dir, 'tree')
  FileUtils.mkdir_p(File.join(tree, 'etc/openssl'))
  File.write(File.join(tree, 'etc/openssl/cert.pem'), 'test trust anchor')
  specs.each_with_index do |name, i|
    entry = File.readlines(File.join(root, name)).select { |s| s.start_with?('./etc/openssl/cert.pem ') || s.start_with?("./etc/openssl/cert.pem\t") }.join
    spec = File.join(dir, 'spec')
    File.write(spec, ". type=dir mode=0755 uname=root gname=wheel\n./etc type=dir mode=0755 uname=root gname=wheel\n./etc/openssl type=dir mode=0755 uname=root gname=wheel\n" + entry)
    out, err, status = Open3.capture3(tool, '-t', 'ffs', '-s', '1m', '-xx', '-F', spec, File.join(dir, "image#{i}"), tree)
    abort "FAIL: #{name} omits the trust bundle: #{out}#{err}" unless status.success?
    puts "PASS: #{name} accepts the trust bundle with strict makefs"
  end
end
